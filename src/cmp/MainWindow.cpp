#include "cmp/MainWindow.h"
#include "cmp/Firmware.h"
#include "cmp/Circuit.h"
#include "cmp/CircuitTree.h"
#include "cmp/CircuitView.h"
#include "cmp/ComponentLibrary.h"
#include "cmp/DigitalDevices.h"
#include "cmp/ElectricalDiagnostics.h"
#include "cmp/ExpandedLibrary.h"
#include "cmp/GlobalSearch.h"
#include "cmp/PackageArtwork.h"
#include "cmp/Recovery.h"
#include "cmp/ResistorBands.h"
#include "cmp/Simulator.h"
#include "cmp/Theme.h"
#include "cmp/ToolIcons.h"
#include "cmp/Waveform.h"
#include "cmp/WindowSizeGrip.h"
#include "cmp/WorkspaceTabs.h"
#include "cmp/WindowHeader.h"
#include <QtWidgets>
#include <cmath>

namespace cmp
{
struct MainWindow::State
{
    MainWindow *w;
    Document doc;
    Simulator simulator;
    std::unique_ptr<Recovery> recovery;
    QString selected = "C1", selectedProbe, phase = "Ready";
    bool ignoring = false, resetPending = false, propertyError = false;
    bool syncingSelection = false, treeOwnsSelection = false;
    CircuitTree *tree = nullptr;
    QToolButton *treeRemove = nullptr;
    QSplitter *inspector = nullptr;
    CircuitView *lastView = nullptr;
    CircuitView *circuit = nullptr, *schematic = nullptr;
    Waveform *plot = nullptr;
    WorkspaceTabs *left = nullptr, *right = nullptr;
    QSplitter *body = nullptr, *center = nullptr, *work = nullptr;
    QWidget *properties = nullptr, *library = nullptr;
    QLabel *project = nullptr, *status = nullptr, *clock = nullptr, *type = nullptr, *pins = nullptr,
           *cursorReadout = nullptr;
    QLineEdit *reference = nullptr, *value = nullptr, *initial = nullptr;
    QLabel *initialLabel = nullptr;
    QToolButton *run = nullptr, *stop = nullptr, *reset = nullptr, *selectionTool = nullptr;
    ComponentLibrary *tiles = nullptr;
    QPlainTextEdit *diagnostics = nullptr;
    QHBoxLayout *legend = nullptr;
    QWidget *legendHost = nullptr;
    QPushButton *pulseSettings = nullptr, *ratingsButton = nullptr;
    QAction *undo = nullptr, *redo = nullptr;
    QVector<QAction *> editingActions;
    explicit State(MainWindow *window) : w(window), doc(window), simulator(window)
    {
        if (QCoreApplication::arguments().contains("--example-lm339"))
        {
            doc.circuit = Circuit::comparator();
            selected = "U1";
        }
    }
    bool busy() const
    {
        return phase == "Running" || phase == "Paused" || phase == "Starting";
    }
    CircuitView *activeView() const
    {
        if (schematic->hasFocus() || (schematic->isVisible() && !circuit->isVisible()))
            return schematic;
        if (circuit->hasFocus())
            return circuit;
        return lastView && lastView->isVisible() ? lastView : circuit;
    }
    void deleteSelection()
    {
        if (treeOwnsSelection || tree->hasFocus())
            tree->deleteSelection();
        else
            activeView()->deleteSelected();
    }
    void message(const QString &s)
    {
        diagnostics->appendPlainText(s);
        status->setText(s.left(160));
    }
    bool discard()
    {
        if (doc.undo.isClean())
            return true;
        auto answer = QMessageBox::question(w, "Unsaved circuit", "Save changes before continuing?",
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save(false));
    }
    void updateTitle()
    {
        QString file = doc.path.isEmpty() ? "Untitled.cmp" : QFileInfo(doc.path).fileName();
        const QString title = file + (doc.undo.isClean() ? "" : " *");
        project->setText(project->fontMetrics().elidedText(title, Qt::ElideMiddle, 180));
        project->setToolTip(title);
        w->setWindowTitle("CMP — " + file + (doc.undo.isClean() ? "" : " *"));
    }
    bool save(bool choose)
    {
        if (propertyError)
        {
            message("Correct the invalid component value before saving.");
            return false;
        }
        QString file = doc.path;
        if (choose || file.isEmpty())
            file = QFileDialog::getSaveFileName(w, "Save circuit", file.isEmpty() ? "555 Blinker.cmp" : file,
                                                "CMP circuits (*.cmp)");
        if (file.isEmpty())
            return false;
        if (!file.endsWith(".cmp", Qt::CaseInsensitive))
            file += ".cmp";
        QString error;
        if (!doc.save(file, error))
        {
            QMessageBox::warning(w, "Save failed", error);
            return false;
        }
        updateTitle();
        message("Saved " + QFileInfo(file).fileName());
        return true;
    }
    void recoverWork(bool startup = false)
    {
        if (!recovery || busy())
            return;
        QStringList errors;
        auto snapshots = recovery->available(errors);
        for (const auto &error : errors)
            message(error);
        if (snapshots.isEmpty())
        {
            if (!startup)
                message("No recoverable snapshots. Snapshots from active CMP windows are excluded.");
            return;
        }
        QDialog dialog(w);
        dialog.setWindowTitle("Recover unsaved work");
        dialog.setObjectName("recoveryDialog");
        auto *layout = new QVBoxLayout(&dialog);
        auto *hint = new QLabel(
            "CMP found unsaved work from an interrupted session. Recovery opens it as unsaved edits; the "
            "original project is unchanged until you save. Later keeps snapshots for another time.");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *list = new QListWidget;
        layout->addWidget(list);
        auto populate = [&]
        {
            list->clear();
            for (const auto &snapshot : snapshots)
            {
                auto *item = new QListWidgetItem(
                    (snapshot.sourcePath.isEmpty() ? "Untitled.cmp"
                                                   : QFileInfo(snapshot.sourcePath).fileName()) +
                        " · " + snapshot.savedAt.toLocalTime().toString(Qt::ISODate),
                    list);
                item->setToolTip(snapshot.sourcePath.isEmpty() ? "Unsaved new circuit" : snapshot.sourcePath);
            }
            if (list->count())
                list->setCurrentRow(0);
        };
        populate();
        auto *buttons = new QDialogButtonBox;
        auto *restore = buttons->addButton("Recover selected", QDialogButtonBox::AcceptRole);
        auto *remove = buttons->addButton("Discard selected", QDialogButtonBox::DestructiveRole);
        auto *later = buttons->addButton("Later", QDialogButtonBox::RejectRole);
        restore->setObjectName("recoverSnapshot");
        later->setObjectName("deferRecovery");
        remove->setObjectName("discardSnapshot");
        layout->addWidget(buttons);
        QObject::connect(restore, &QPushButton::clicked, &dialog, &QDialog::accept);
        QObject::connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(remove, &QPushButton::clicked, &dialog,
                         [&]
                         {
                             const int row = list->currentRow();
                             if (row >= 0 && recovery->remove(snapshots[row]))
                             {
                                 snapshots.removeAt(row);
                                 populate();
                                 if (snapshots.isEmpty())
                                     dialog.reject();
                             }
                         });
        dialog.resize(570, 300);
        if (dialog.exec() != QDialog::Accepted || list->currentRow() < 0 || !discard())
            return;
        if (recovery->restore(snapshots[list->currentRow()]))
        {
            selected.clear();
            selectedProbe.clear();
            circuit->fit();
            schematic->fit();
            refreshProperties();
            message("Recovered unsaved work. Save to commit it to a project file.");
        }
    }
    void loadExample(Circuit c)
    {
        if (busy() || !discard())
            return;
        doc.replace(c);
        selected = c.part("C1") ? "C1" : QString();
        QTimer::singleShot(0, w,
                           [this]
                           {
                               circuit->fit();
                               schematic->fit();
                               activeView()->selectPart(selected);
                               refreshProperties();
                           });
    }
    void refreshProperties()
    {
        propertyError = false;
        setInvalidInput(value, false);
        setInvalidInput(initial, false);
        auto *p = doc.circuit.part(selected);
        ignoring = true;
        const bool probe = !selectedProbe.isEmpty() && doc.circuit.probes.contains(selectedProbe);
        reference->setReadOnly(probe);
        reference->setToolTip(probe ? "Voltage probe name. Delete or locate the probe from the circuit tree."
                                    : "Unique component reference; letters, digits and underscores.");
        ratingsButton->setVisible(!probe && p && !ratingDefinitions(p->kind).isEmpty());
        ratingsButton->setEnabled(!busy());
        if (probe)
        {
            const auto pin = doc.circuit.probes.value(selectedProbe);
            const auto *part = doc.circuit.part(pin.part);
            type->setText("Voltage probe");
            reference->setText(selectedProbe);
            value->clear();
            value->setEnabled(false);
            initial->clear();
            initial->setEnabled(false);
            pulseSettings->hide();
            pins->setText("Attached to " + pin.part + " · " +
                          (part ? spec(part->kind).pins.value(pin.index) : QString::number(pin.index)) +
                          "\n\nDelete the probe from the circuit tree to remove its voltage trace.");
            ignoring = false;
            return;
        }
        reference->setText(p ? p->id : QString());
        type->setText(p ? spec(p->kind).name : "Select a component");
        bool scalar = p && (p->kind == "R" || p->kind == "C" || p->kind == "CP" || p->kind == "V" ||
                            p->kind == "PULSE" || !spec(p->kind).unit.isEmpty());
        value->setText(scalar ? quantity(p->value, p->kind) : QString());
        value->setEnabled(scalar && !busy());
        bool cap = p && (p->kind == "C" || p->kind == "CP" || spec(p->kind).behavior == "capacitor" ||
                         spec(p->kind).behavior == "polarCap" || spec(p->kind).behavior == "inductor");
        initialLabel->setText(p && p->kind == "L" ? "Initial current" : "Initial voltage");
        initial->setEnabled(cap && !busy());
        initial->setText(cap ? quantity(p->initial, p->kind == "L" ? "I" : "V") : QString());
        pulseSettings->setVisible(p && (p->kind == "PULSE" || !spec(p->kind).settings.isEmpty() ||
                                        !spec(p->kind).dataKind.isEmpty()));
        pulseSettings->setText(p && p->kind == "PULSE" ? "Pulse settings…" : "Component settings…");
        pulseSettings->setEnabled(p && !busy());
        pins->setText(p ? "Pins\n" + spec(p->kind).pins.join("  ·  ")
                        : "Click a component to inspect its parameters.\n\nClick a library tile, then the "
                          "workspace to place it.");
        ignoring = false;
    }
    void editRatings()
    {
        const auto *part = doc.circuit.part(selected);
        if (!part || busy())
            return;
        const QString id = part->id;
        const auto definitions = ratingDefinitions(part->kind);
        QDialog dialog(w);
        dialog.setWindowTitle(id + " · Electrical ratings");
        auto *layout = new QVBoxLayout(&dialog);
        auto *explanation = new QLabel(
            "Limits are independent of component value. Blank means unspecified for generic parts, or the "
            "reference-device preset shown below. Confirm limits for your chosen physical part. Checks use "
            "sampled values; no thermal derating or damage is simulated.");
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto *form = new QFormLayout;
        QMap<QString, QLineEdit *> fields;
        for (const auto &d : definitions)
        {
            auto *edit = new QLineEdit;
            edit->setObjectName("rating_" + d.key);
            if (part->ratings.contains(d.key))
                edit->setText(QString::number(part->ratings.value(d.key), 'g', 12));
            edit->setPlaceholderText(
                d.preset > 0 ? QString("Reference preset: %1 %2").arg(d.preset).arg(d.unit) : "Unspecified");
            form->addRow(d.label + " (" + d.unit + ")", edit);
            fields[d.key] = edit;
        }
        layout->addLayout(form);
        if (part->kind == "LM339")
            layout->addWidget(new QLabel("Classic LM339N: shared supply 2–30 V; inputs 0 to VCC−1.5 V. "
                                         "External output bias required."));
        if (part->kind == "555")
            layout->addWidget(new QLabel("NE555 recommended supply: 4.5–16 V (checked separately)."));
        auto *error = new QLabel;
        error->setWordWrap(true);
        layout->addWidget(error);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                         [&]
                         {
                             QMap<QString, double> ratings;
                             for (const auto &d : definitions)
                             {
                                 const auto text = fields[d.key]->text().trimmed();
                                 if (text.isEmpty())
                                     continue;
                                 bool ok = false;
                                 const double value = text.toDouble(&ok);
                                 if (!ok || !std::isfinite(value) || value <= 0)
                                 {
                                     error->setText(d.label + ": enter a positive finite number in " +
                                                    d.unit + ".");
                                     fields[d.key]->setFocus();
                                     return;
                                 }
                                 ratings[d.key] = value;
                             }
                             doc.edit("Electrical ratings " + id,
                                      [=](Circuit &c)
                                      {
                                          if (auto *p = c.part(id))
                                              p->ratings = ratings;
                                      });
                             dialog.accept();
                         });
        dialog.resize(460, dialog.sizeHint().height());
        dialog.exec();
    }
    void applyProperty(bool initialValue)
    {
        if (ignoring || busy())
            return;
        auto *p = doc.circuit.part(selected);
        if (!p)
            return;
        auto *edit = initialValue ? initial : value;
        double number = 0;
        QString text = edit->text().trimmed();
        QString expected = expanded(p->kind) && !initialValue  ? spec(p->kind).unit
                           : initialValue                      ? (p->kind == "L" ? "A" : "V")
                           : p->kind == "R"                    ? "Ω"
                           : p->kind == "C" || p->kind == "CP" ? "F"
                                                               : "V";
        bool wrongUnit = (text.endsWith('F') && expected != "F") || (text.endsWith('V') && expected != "V") ||
                         ((text.endsWith("Ω") || text.endsWith("ohm")) && expected != "Ω");
        if (wrongUnit || !parseQuantityUnit(text, expected, number) ||
            (!initialValue && (p->kind == "R" || p->kind == "C" || p->kind == "CP") && number <= 0))
        {
            propertyError = true;
            setInvalidInput(edit, true);
            message("Invalid value. Use matching SI units such as 10 kΩ, 10 uF or 5 V; passive values must "
                    "be positive.");
            return;
        }
        propertyError = false;
        setInvalidInput(edit, false);
        QString id = selected;
        doc.edit("Edit " + id,
                 [=](Circuit &c)
                 {
                     if (auto *part = c.part(id))
                     {
                         if (initialValue)
                             part->initial = number;
                         else
                             part->value = number;
                     }
                 });
    }
    void renamePart()
    {
        if (ignoring || busy() || !doc.circuit.part(selected))
            return;
        QString next = reference->text().trimmed(), old = selected;
        if (next == old)
            return;
        bool duplicate = false;
        for (const auto &part : doc.circuit.parts)
            duplicate |= part.id != old && part.id.compare(next, Qt::CaseInsensitive) == 0;
        if (!QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,31}$").match(next).hasMatch() || duplicate)
        {
            reference->setText(old);
            message("Reference must be unique and contain only letters, digits and underscores (start with a "
                    "letter).");
            return;
        }
        selected = next;
        doc.edit("Rename " + old,
                 [=](Circuit &c)
                 {
                     c.part(old)->id = next;
                     for (auto &wire : c.wires)
                     {
                         if (wire.a.part == old)
                             wire.a.part = next;
                         if (wire.b.part == old)
                             wire.b.part = next;
                     }
                     for (auto &label : c.labels)
                         if (label.pin.part == old)
                             label.pin.part = next;
                     for (auto it = c.probes.begin(); it != c.probes.end(); ++it)
                         if (it.value().part == old)
                             it.value().part = next;
                 });
        circuit->selectPart(next);
        schematic->selectPart(next);
    }
    void updateTraces()
    {
        while (auto *item = legend->takeAt(0))
        {
            if (item->widget())
                delete item->widget();
            delete item;
        }
        plot->traces.clear();
        auto nodes = doc.circuit.nodes();
        const auto &colors = theme().traces;
        int i = 0;
        auto add = [&](QString name, QString key, bool current, bool visible)
        {
            Trace trace{name, key, current && visible ? theme().good : colors[i % colors.size()], visible,
                        current};
            ++i;
            plot->traces.append(trace);
            auto *check = new QCheckBox(name);
            check->setChecked(visible);
            check->setStyleSheet("color:" + trace.color.name() + ";");
            int index = plot->traces.size() - 1;
            check->setProperty("traceIndex", index);
            legend->addWidget(check);
            QObject::connect(check, &QCheckBox::toggled, w,
                             [this, index](bool on)
                             {
                                 plot->traces[index].visible = on;
                                 plot->update();
                             });
        };
        for (auto it = doc.circuit.probes.begin(); it != doc.circuit.probes.end(); ++it)
            add("V(" + it.key() + ")", nodes.value(it.value().key()), false, it.key() != "vcc");
        // A current probe is available for each passive branch and independent source.
        for (auto &p : doc.circuit.parts)
            if (!expanded(p.kind) && p.kind != "GND" && p.kind != "555" && p.kind != "NPN" &&
                spec(p.kind).comparators.isEmpty())
                add("I(" + p.id + ")",
                    p.kind == "V" || p.kind == "PULSE" ? "v" + p.id.toLower() + "#branch"
                                                       : "vi_" + p.id.toLower() + "#branch",
                    true, p.kind == "LED");
        for (const auto &p : doc.circuit.parts)
            for (const auto &ch :
                 (expanded(p.kind) ? QVector<ComparatorChannel>{} : spec(p.kind).comparators))
                add("I(" + p.id + "." + QString::number(ch.output + 1) + ")",
                    "voc_" + p.id.toLower() + "_" + QString::number(ch.output + 1) + "#branch", true, false);
        for (const auto &p : doc.circuit.parts)
            if (expanded(p.kind) && measuresPins(p.kind))
                add("I(" + p.id + ")", currentKey(p), true, spec(p.kind).behavior == "led");
        legend->addStretch();
        plot->duration = doc.circuit.duration;
        plot->resetZoom();
    }
    void ledState(const Sample &s)
    {
        QMap<QString, double> current;
        for (auto &p : doc.circuit.parts)
            if (p.kind == "LED" || spec(p.kind).behavior == "led")
                current[p.id] =
                    s.values.value(expanded(p.kind) ? currentKey(p) : "vi_" + p.id.toLower() + "#branch");
        circuit->setLedCurrents(current);
        schematic->setLedCurrents(current);
    }
    void readCursor(double t, const Sample &s)
    {
        circuit->setDeviceStates(s.values);
        schematic->setDeviceStates(s.values);
        circuit->setSimulationTime(t);
        schematic->setSimulationTime(t);
        QStringList values;
        for (auto &trace : plot->traces)
            if (trace.visible)
                values << trace.name + " = " +
                              QString::number(s.values.value(trace.key) * (trace.current ? 1000 : 1), 'g',
                                              5) +
                              (trace.current ? " mA" : " V");
        for (const auto &part : doc.circuit.parts)
            if (part.kind == "LPROBE")
            {
                QString pattern;
                for (int pin = 7; pin >= 0; --pin)
                {
                    int state = s.values.value("logic_" + part.id.toLower() + "_" + QString::number(pin), 3);
                    pattern += state == 0 ? '0' : state == 1 ? '1' : state == 2 ? 'Z' : 'X';
                }
                values << part.id + " = " + pattern;
            }
        cursorReadout->setText(QString::number(t, 'f', 5) + " s   " + values.join("   "));
        ledState(s);
    }
    void setState(QString s)
    {
        phase = s;
        bool locked = busy();
        doc.editsLocked = locked;
        if (!locked)
            selectionTool->setChecked(true);
        circuit->setLocked(locked);
        schematic->setLocked(locked);
        library->setEnabled(!locked);
        properties->setEnabled(!locked);
        treeRemove->setEnabled(!locked && !tree->selection().empty());
        for (auto *a : editingActions)
            a->setEnabled(!locked);
        undo->setEnabled(!locked && doc.undo.canUndo());
        redo->setEnabled(!locked && doc.undo.canRedo());
        run->setEnabled(s != "Starting");
        stop->setEnabled(locked);
        run->setIcon(toolIcon(s == "Running" ? "pause" : "play"));
        run->setToolTip(s == "Running"  ? "Pause simulation (F5)"
                        : s == "Paused" ? "Resume simulation (F5)"
                                        : "Run simulation (F5)");
        run->setAccessibleName(run->toolTip());
        status->setText(s);
        refreshProperties();
    }
    void toggleRun()
    {
        if (phase == "Running")
        {
            simulator.pause();
            return;
        }
        if (phase == "Paused")
        {
            simulator.resume();
            return;
        }
        if (phase == "Starting")
            return;
        // Uncommitted text must never silently run using an older component value.
        if (value->hasFocus())
            applyProperty(false);
        if (initial->hasFocus())
            applyProperty(true);
        if (propertyError)
        {
            message("Correct the invalid component value before running.");
            return;
        }
        auto errors = doc.circuit.validate();
        if (!errors.isEmpty())
        {
            diagnostics->setPlainText(errors.join('\n'));
            message("Circuit validation failed — see Diagnostics.");
            return;
        }
        resetPending = false;
        plot->clear();
        diagnostics->clear();
        clock->setText("0.000 s");
        setState("Starting");
        simulator.start(doc.circuit, true);
    }
    void resetRun()
    {
        resetPending = true;
        simulator.stop();
        plot->clear();
        clock->setText("0.000 s");
        cursorReadout->clear();
        ledState({});
        circuit->setDeviceStates({});
        schematic->setDeviceStates({});
        circuit->setSimulationTime(0);
        schematic->setSimulationTime(0);
        setState("Ready");
    }
    void splitViews()
    {
        if (left->count() == 0)
            mergeViews();
        if (left->count() > 1)
        {
            auto *p = schematic;
            int index = left->indexOf(p);
            QString title = left->tabText(index);
            left->removeTab(index);
            right->addTab(p, title);
        }
        work->setOrientation(Qt::Horizontal);
        right->show();
        left->show();
        work->setSizes({500, 500});
        QTimer::singleShot(0, w,
                           [this]
                           {
                               circuit->fit();
                               schematic->fit();
                           });
    }
    void mergeViews()
    {
        while (right->count())
        {
            auto *p = right->widget(0);
            QString title = right->tabText(0);
            right->removeTab(0);
            left->addTab(p, title);
        }
        right->hide();
        left->show();
        left->setCurrentWidget(circuit);
        QTimer::singleShot(0, w,
                           [this]
                           {
                               circuit->fit();
                               schematic->fit();
                           });
    }
    void setTool(const QString &name)
    {
        circuit->setTool(name);
        schematic->setTool(name);
        if (name == "wire")
            message("Click a component pin, then another pin to connect. Crossing wires do not connect.");
        if (name == "probe")
            message("Click a component pin to add a named voltage probe.");
    }
    void componentSettings()
    {
        const auto *part = doc.circuit.part(selected);
        if (!part || busy())
            return;
        const auto id = selected;
        const auto definition = spec(part->kind);
        QDialog dialog(w);
        dialog.setWindowTitle(id + " · Component settings");
        auto *layout = new QFormLayout(&dialog);
        QMap<QString, QLineEdit *> fields;
        for (const auto &setting : definition.settings)
        {
            auto *field = new QLineEdit(QString::number(parameter(*part, setting.key), 'g', 14));
            field->setObjectName("parameter_" + setting.key);
            fields[setting.key] = field;
            layout->addRow(setting.label + (setting.unit.isEmpty() ? "" : " (" + setting.unit + ")"), field);
        }
        if (!definition.limitations.isEmpty())
        {
            auto *modelInfo = new QLabel(definition.limitations);
            modelInfo->setWordWrap(true);
            modelInfo->setMaximumWidth(620);
            layout->addRow("Model", modelInfo);
        }
        QPlainTextEdit *payload = nullptr;
        if (definition.dataKind == "pwl" || definition.dataKind == "curve")
        {
            payload = new QPlainTextEdit(
                part->data.isEmpty() ? (definition.dataKind == "pwl" ? "0 0\n0.1 5\n0.2 0" : "0 0.5\n100 4.5")
                                     : QString::fromUtf8(part->data));
            payload->setObjectName("componentCurve");
            payload->setMinimumSize(350, 160);
            layout->addRow(definition.dataKind == "pwl" ? "Time (s), voltage (V)" : "Input, output (V)",
                           payload);
        }
        QByteArray firmware = part->data;
        auto *error = new QLabel;
        error->setWordWrap(true);
        if (definition.dataKind == "firmware" || definition.dataKind == "memory")
        {
            auto *summary =
                new QLabel(firmware.isEmpty() ? "No firmware loaded"
                                              : QString::number(firmware.size()) + " bytes loaded");
            layout->addRow("Program flash", summary);
            auto *import = new QPushButton("Import .hex / .bin…");
            layout->addRow(import);
            QObject::connect(import, &QPushButton::clicked, &dialog,
                             [&]
                             {
                                 auto path =
                                     QFileDialog::getOpenFileName(&dialog, "Import processor firmware", {},
                                                                  "Firmware (*.hex *.ihx *.bin)");
                                 if (path.isEmpty())
                                     return;
                                 QFile file(path);
                                 if (!file.open(QIODevice::ReadOnly) || file.size() > 262144)
                                 {
                                     error->setText("Cannot read firmware or file exceeds import limit.");
                                     return;
                                 }
                                 QByteArray image;
                                 QString why;
                                 bool hex = !path.endsWith(".bin", Qt::CaseInsensitive);
                                 if (!parseFirmware(file.readAll(), hex, image, why,
                                                    definition.dataKind == "memory" ? 256 : 32768))
                                 {
                                     error->setText(why);
                                     return;
                                 }
                                 firmware = image;
                                 error->clear();
                                 summary->setText(QString::number(firmware.size()) + " bytes · " +
                                                  QFileInfo(path).fileName());
                             });
        }
        layout->addRow(error);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                         [&]
                         {
                             Part proposed = *doc.circuit.part(id);
                             for (const auto &setting : definition.settings)
                             {
                                 double value;
                                 if (!parseQuantityUnit(fields[setting.key]->text(), setting.unit, value))
                                 {
                                     error->setText("Enter finite SI values.");
                                     return;
                                 }
                                 proposed.parameters[setting.key] = value;
                             }
                             if (payload)
                             {
                                 proposed.data = payload->toPlainText().toUtf8();
                                 proposed.dataType = definition.dataKind;
                             }
                             if (definition.dataKind == "firmware" || definition.dataKind == "memory")
                             {
                                 proposed.data = firmware;
                                 proposed.dataType = firmware.isEmpty() ? QString() : definition.dataKind;
                             }
                             const auto errors = validateExpanded(proposed);
                             if (!errors.isEmpty())
                             {
                                 error->setText(errors.join("\n"));
                                 return;
                             }
                             doc.edit("Component settings " + id,
                                      [=](Circuit &c)
                                      {
                                          c.part(id)->parameters = proposed.parameters;
                                          c.part(id)->data = proposed.data;
                                          c.part(id)->dataType = proposed.dataType;
                                      });
                             dialog.accept();
                         });
        dialog.exec();
    }
    void pulseDialog()
    {
        auto *part = doc.circuit.part(selected);
        if (part && expanded(part->kind))
        {
            componentSettings();
            return;
        }
        if (!part || part->kind != "PULSE" || busy())
            return;
        QDialog dialog(w);
        dialog.setWindowTitle("Pulse source settings");
        auto *layout = new QFormLayout(&dialog);
        QMap<QString, QLineEdit *> fields;
        for (auto pair : QVector<QPair<QString, double>>{
                 {"low", 0}, {"delay", .2}, {"rise", 1e-6}, {"fall", 1e-6}, {"width", .5}, {"period", 1}})
        {
            auto *input =
                new QLineEdit(QString::number(part->parameters.value(pair.first, pair.second), 'g', 10));
            fields[pair.first] = input;
            layout->addRow(pair.first + (pair.first == "low" ? " (V)" : " (s)"), input);
        }
        layout->addRow(new QLabel("Use numeric SI notation, e.g. 1u for 1 µs."));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QMap<QString, double> values;
        for (auto it = fields.begin(); it != fields.end(); ++it)
        {
            double v;
            if (!parseQuantity(it.value()->text(), v))
            {
                QMessageBox::warning(w, "Invalid pulse", "Use numeric SI values for each field.");
                return;
            }
            values[it.key()] = v;
        }
        if (values["rise"] <= 0 || values["fall"] <= 0 || values["delay"] < 0 || values["width"] <= 0 ||
            values["period"] < values["rise"] + values["width"] + values["fall"])
        {
            QMessageBox::warning(
                w, "Invalid pulse",
                "Period must contain positive rise, width and fall times; delay must be non-negative.");
            return;
        }
        auto id = selected;
        doc.edit("Pulse settings", [=](Circuit &c) { c.part(id)->parameters = values; });
    }
    void analysisDialog()
    {
        if (busy())
            return;
        QDialog dialog(w);
        dialog.setWindowTitle("Analysis settings");
        auto *layout = new QFormLayout(&dialog);
        auto *mode = new QComboBox;
        mode->addItems({"Transient", "DC operating point"});
        mode->setCurrentIndex(doc.circuit.operatingPoint ? 1 : 0);
        auto *duration = new QDoubleSpinBox;
        duration->setRange(.001, 60);
        duration->setDecimals(3);
        duration->setValue(doc.circuit.duration);
        duration->setSuffix(" s");
        auto *step = new QLineEdit(QString::number(doc.circuit.step, 'g', 8));
        layout->addRow("Analysis", mode);
        layout->addRow("Duration", duration);
        layout->addRow("Maximum step (s)", step);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
        {
            double dt = 0;
            if (!parseQuantity(step->text(), dt) || dt <= 0 || dt > duration->value() ||
                duration->value() / dt > 200000)
            {
                QMessageBox::warning(
                    w, "Invalid analysis",
                    "Use a positive step no larger than duration, with at most 200,000 nominal steps.");
                return;
            }
            double seconds = duration->value();
            bool op = mode->currentIndex() == 1;
            doc.edit("Analysis settings",
                     [=](Circuit &c)
                     {
                         c.duration = seconds;
                         c.step = dt;
                         c.operatingPoint = op;
                     });
        }
    }
};
MainWindow::MainWindow() : state(new State(this))
{
    auto *s = state.get();
    appearance_ = QSettings().value("appearance/mode", "system").toString();
    if (appearance_ != "light" && appearance_ != "dark")
        appearance_ = "system";
    installTheme(appearanceIsDark(appearance_, QGuiApplication::styleHints()->colorScheme()));
    extendedHeader_ = WindowHeader::supportsExpandedTitleBar();
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    if (extendedHeader_)
    {
        auto flags = windowFlags() | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint |
                     Qt::CustomizeWindowHint | Qt::WindowSystemMenuHint;
        flags &= ~(Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint |
                   Qt::WindowTitleHint);
        setWindowFlags(flags);
        setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    }
#endif
    resize(1440, 960);
    setMinimumSize(1050, 700);
    setWindowIcon(QIcon(":/cmp/logo.png"));
    auto title = [](QString text)
    {
        auto *l = new QLabel(text);
        l->setObjectName("panelTitle");
        return l;
    };
    auto *shell = new QWidget;
    auto *outer = new QVBoxLayout(shell);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    setCentralWidget(shell);
    header_ = new WindowHeader;
    auto *hr = header_->row();
    auto *logo = new QLabel;
    logo->setObjectName("brandIcon");
    logo->setFixedSize(24, 24);
    logo->setAttribute(Qt::WA_TransparentForMouseEvents);
    logo->setPixmap(QPixmap(":/cmp/logo.png").scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setToolTip("Cinderluna Microcontroller Playground");
    hr->addWidget(logo);
    appMenu_ = new QMenuBar(header_);
    appMenu_->setObjectName("titleMenus");
    appMenu_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    hr->addWidget(appMenu_, 0, Qt::AlignVCenter);
    auto *separator = new QFrame;
    separator->setObjectName("titleMenuSeparator");
    separator->setFixedSize(1, 22);

    separator->setAttribute(Qt::WA_TransparentForMouseEvents);
    hr->addSpacing(8);
    hr->addWidget(separator, 0, Qt::AlignVCenter);
    hr->addSpacing(8);
    s->project = new QLabel;
    s->project->setObjectName("projectName");
    s->project->setMinimumWidth(0);
    s->project->setMaximumWidth(180);
    s->project->setAttribute(Qt::WA_TransparentForMouseEvents);
    hr->addWidget(s->project);
    hr->addStretch(2);
    search_ = new GlobalSearch;
    search_->setMinimumWidth(200);
    search_->setMaximumWidth(300);
    search_->setFixedHeight(32);
    hr->addWidget(search_, 1, Qt::AlignVCenter);
    if (extendedHeader_)
        header_->installCaptionControls();
    outer->addWidget(header_);
    auto *toolbar = new QWidget;
    toolbar->setObjectName("mainToolbar");
    auto *tr = new QHBoxLayout(toolbar);
    tr->setContentsMargins(12, 6, 12, 6);
    tr->setSpacing(4);
    auto button = [&](QString name, const QString &icon)
    {
        auto *b = new QToolButton;
        b->setToolTip(name);
        b->setAccessibleName(name);
        b->setIcon(toolIcon(icon));
        b->setObjectName("tool_" + icon);
        b->setIconSize(QSize(20, 20));
        b->setFixedSize(40, 36);
        tr->addWidget(b);
        return b;
    };
    auto *select = button("Select components", "cursor");
    s->selectionTool = select;
    auto *wire = button("Connect pins", "wire");
    auto *probe = button("Add voltage probe", "probe");
    auto *rotate = button("Rotate selection (R)", "rotate");
    auto *remove = button("Delete selection", "delete");
    auto *fit = button("Fit workspace", "fit");
    auto *split = button("Split views", "split");
    auto *merge = button("Merge views", "merge");
    auto *tools = new QButtonGroup(this);
    tools->setExclusive(true);
    for (auto *b : {select, wire, probe})
    {
        b->setCheckable(true);
        tools->addButton(b);
    }
    select->setChecked(true);
    tr->addStretch();
    auto *analysis = button("Analysis settings", "settings");
    s->run = button("Run simulation (F5)", "play");
    s->run->setObjectName("runButton");
    s->run->setShortcut(Qt::Key_F5);
    s->stop = button("Stop simulation", "stop");
    s->reset = button("Reset simulation", "reset");
    outer->addWidget(toolbar);
    s->body = new QSplitter;
    s->body->setHandleWidth(1);
    outer->addWidget(s->body, 1);
    s->inspector = new QSplitter(Qt::Vertical);
    s->inspector->setObjectName("inspectorSplitter");
    s->inspector->setHandleWidth(3);
    s->inspector->setMinimumWidth(220);
    auto *treePane = new QWidget;
    auto *treeLayout = new QVBoxLayout(treePane);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(0);
    auto *treeHeaderWidget = new QWidget;
    treeHeaderWidget->setObjectName("circuitTreeHeader");
    auto *treeHeader = new QHBoxLayout(treeHeaderWidget);
    treeHeader->setContentsMargins(0, 0, 6, 0);
    treeHeader->addWidget(title("Circuit"), 1);
    s->treeRemove = new QToolButton;
    s->treeRemove->setObjectName("circuitTreeDelete");
    s->treeRemove->setIcon(toolIcon("delete"));
    s->treeRemove->setFixedSize(30, 30);
    s->treeRemove->setToolTip("Delete selected circuit components or probes");
    s->treeRemove->setAccessibleName("Delete selected circuit items");
    treeHeader->addWidget(s->treeRemove);
    treeLayout->addWidget(treeHeaderWidget);
    s->tree = new CircuitTree(&s->doc);
    treeLayout->addWidget(s->tree, 1);
    treePane->setMinimumHeight(140);
    s->inspector->addWidget(treePane);
    s->properties = new QWidget;
    auto *pl = new QVBoxLayout(s->properties);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(title("Properties"));
    auto *form = new QFormLayout;
    form->setContentsMargins(14, 18, 14, 12);
    form->setVerticalSpacing(16);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    s->type = new QLabel;
    s->type->setWordWrap(true);
    s->reference = new QLineEdit;
    s->reference->setToolTip("Unique component reference; letters, digits and underscores.");
    s->value = new QLineEdit;
    s->value->setObjectName("componentValue");
    s->value->setToolTip("SI syntax: 10k, 10 kΩ, 10 uF, 5 V. m = milli, M = mega.");
    s->initial = new QLineEdit;
    form->addRow("Type", s->type);
    form->addRow("Reference", s->reference);
    form->addRow("Value", s->value);
    s->initialLabel = new QLabel("Initial voltage");
    form->addRow(s->initialLabel, s->initial);
    pl->addLayout(form);
    s->pulseSettings = new QPushButton("Pulse settings…");
    pl->addWidget(s->pulseSettings);
    connect(s->pulseSettings, &QPushButton::clicked, this, [=] { s->pulseDialog(); });
    s->ratingsButton = new QPushButton("Electrical ratings…");
    pl->addWidget(s->ratingsButton);
    connect(s->ratingsButton, &QPushButton::clicked, this, [=] { s->editRatings(); });
    s->pins = new QLabel;
    s->pins->setWordWrap(true);
    s->pins->setObjectName("pinDetails");
    pl->addWidget(s->pins);
    pl->addStretch();
    auto *propertyScroll = new QScrollArea;
    propertyScroll->setWidgetResizable(true);
    propertyScroll->setFrameShape(QFrame::NoFrame);
    propertyScroll->setMinimumHeight(220);
    propertyScroll->setWidget(s->properties);
    s->inspector->addWidget(propertyScroll);
    s->inspector->setSizes({400, 420});
    s->body->addWidget(s->inspector);
    s->center = new QSplitter(Qt::Vertical);
    s->center->setHandleWidth(1);
    s->body->addWidget(s->center);
    s->work = new QSplitter;
    s->work->setHandleWidth(3);
    s->center->addWidget(s->work);
    s->left = new WorkspaceTabs;
    s->left->setObjectName("workspaceTabs");
    s->right = new WorkspaceTabs;
    s->right->setObjectName("secondaryTabs");
    s->work->addWidget(s->left);
    s->work->addWidget(s->right);
    s->right->hide();
    s->circuit = new CircuitView(&s->doc, true);
    s->schematic = new CircuitView(&s->doc, false);
    s->left->addTab(s->circuit, "Circuit view");
    s->left->addTab(s->schematic, "Schematic view");
    auto move = [=](WorkspaceTabs *from, QWidget *page, WorkspaceTabs *to)
    {
        QString t = from->tabText(from->indexOf(page));
        from->removeTab(from->indexOf(page));
        to->addTab(page, t);
        to->setCurrentWidget(page);
    };
    auto dock = [=](WorkspaceTabs *from, QWidget *page, WorkspaceTabs *to, Qt::Edge edge)
    {
        if (edge)
        {
            if (from == to && from->count() < 2)
                return;
            auto *target = to == s->left ? s->right : s->left;
            if (!target->isHidden() && target != from)
                return;
            s->work->setOrientation(edge == Qt::TopEdge || edge == Qt::BottomEdge ? Qt::Vertical
                                                                                  : Qt::Horizontal);
            move(from, page, target);
            target->show();
            s->work->insertWidget(edge == Qt::LeftEdge || edge == Qt::TopEdge ? 0 : 1, target);
            s->work->setSizes({500, 500});
        }
        else if (from != to)
            move(from, page, to);
        if (!s->left->count())
            s->left->hide();
        if (!s->right->count())
            s->right->hide();
    };
    s->left->dockRequested = dock;
    s->right->dockRequested = dock;
    auto canDock = [=](WorkspaceTabs *from, QWidget *, WorkspaceTabs *to, Qt::Edge edge)
    {
        if (!edge)
            return true;
        if (from == to && from->count() < 2)
            return false;
        auto *other = to == s->left ? s->right : s->left;
        return other->isHidden() || other == from;
    };
    s->left->canDock = canDock;
    s->right->canDock = canDock;
    auto *instruments = new QTabWidget;
    auto *wavePage = new QWidget;
    auto *wl = new QVBoxLayout(wavePage);
    wl->setContentsMargins(12, 4, 12, 4);
    s->legendHost = new QWidget;
    s->legend = new QHBoxLayout(s->legendHost);
    s->legend->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(s->legendHost);
    scroll->setFixedHeight(43);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    wl->addWidget(scroll);
    s->plot = new Waveform;
    wl->addWidget(s->plot, 1);
    s->cursorReadout = new QLabel;
    s->cursorReadout->setWordWrap(true);
    s->cursorReadout->setObjectName("cursorReadout");
    wl->addWidget(s->cursorReadout);
    instruments->addTab(wavePage, "Waveforms");
    s->diagnostics = new QPlainTextEdit;
    s->diagnostics->setReadOnly(true);
    s->diagnostics->setMaximumBlockCount(1000);
    instruments->addTab(s->diagnostics, "Diagnostics");
    s->center->addWidget(instruments);
    s->center->setSizes({580, 250});
    s->library = new QWidget;
    auto *ll = new QVBoxLayout(s->library);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(title("Components"));
    auto *filter = new QLineEdit;
    filter->setPlaceholderText("Search components…");
    filter->setClearButtonEnabled(true);
    auto *fl = new QHBoxLayout;
    fl->setContentsMargins(12, 8, 12, 8);
    fl->addWidget(filter);
    ll->addLayout(fl);
    s->tiles = new ComponentLibrary;
    s->tiles->setObjectName("componentLibrary");
    s->tiles->setViewMode(QListView::IconMode);
    // ComponentLibrary performs one explicit layout after updating its grid;
    // Qt's automatic pass would first reflow using the previous cell width.
    s->tiles->setResizeMode(QListView::Fixed);
    s->tiles->setMovement(QListView::Static);
    s->tiles->setWrapping(true);
    s->tiles->setWordWrap(true);
    s->tiles->setIconSize(QSize(62, 62));
    s->tiles->setGridSize(QSize(88, 112));
    s->tiles->setSpacing(0);
    s->tiles->setUniformItemSizes(false);
    s->tiles->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QPixmap atlas(":/cmp/components.png");
    QStringList categories;
    for (const auto &part : catalog())
        if (!categories.contains(componentCategory(part.kind)))
            categories << componentCategory(part.kind);
    for (const auto &category : categories)
    {
        s->tiles->addCategory(category);
        for (const auto &part : catalog())
        {
            if (componentCategory(part.kind) != category)
                continue;
            int i = part.sprite;
            int x0 = (i % 5) * atlas.width() / 5, x1 = (i % 5 + 1) * atlas.width() / 5,
                y0 = (i / 5) * atlas.height() / 2, y1 = (i / 5 + 1) * atlas.height() / 2;
            QPixmap artwork = part.sprite == -2 ? expandedIcon(part)
                              : part.sprite < 0 ? dipIcon("LM339", 14)
                                                : atlas.copy(x0, y0, x1 - x0, y1 - y0);
            if (part.kind == "R")
            {
                artwork.fill(Qt::transparent);
                QPainter painter(&artwork);
                painter.scale(artwork.width() / 86., artwork.height() / 86.);
                painter.translate(43, 43);
                paintResistor(painter, part.value);
            }
            auto *item = new QListWidgetItem(QIcon(artwork), part.name, s->tiles);
            item->setData(Qt::UserRole, part.kind);
            item->setData(ComponentLibrary::CategoryRole, category);
            item->setSizeHint(QSize(82, 106));
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            item->setToolTip(
                part.comparators.isEmpty()
                    ? part.name + " — click, then place in workspace"
                    : part.package +
                          QString(" · %1 open-collector channel(s); external output bias required.")
                              .arg(part.comparators.size()));
        }
    }
    s->tiles->arrange();
    ll->addWidget(s->tiles, 1);
    auto *hint = new QLabel("Drag a tile for one part. Click a tile for repeat placement.");
    hint->setWordWrap(true);
    hint->setObjectName("componentHint");
    ll->addWidget(hint);
    s->body->addWidget(s->library);
    s->library->setMinimumWidth(210);
    s->body->setSizes({245, 895, 300});
    s->body->setStretchFactor(1, 1);
    statusBar()->setSizeGripEnabled(false);
    s->status = new QLabel("Ready");
    statusBar()->addWidget(s->status, 1);
    s->clock = new QLabel("0.000 s");
    statusBar()->addPermanentWidget(s->clock);
    statusBar()->addPermanentWidget(new QLabel("  ngspice 47  "));
    statusBar()->addPermanentWidget(new WindowSizeGrip(statusBar()));
    connect(select, &QToolButton::clicked, this, [=] { s->setTool("select"); });
    connect(wire, &QToolButton::clicked, this, [=] { s->setTool("wire"); });
    connect(probe, &QToolButton::clicked, this, [=] { s->setTool("probe"); });
    connect(rotate, &QToolButton::clicked, this, [=] { s->activeView()->rotateSelected(); });
    connect(remove, &QToolButton::clicked, this, [=] { s->deleteSelection(); });
    connect(s->treeRemove, &QToolButton::clicked, s->tree, &CircuitTree::deleteSelection);
    connect(s->tree, &CircuitTree::circuitSelectionChanged, this,
            [=]
            {
                if (s->syncingSelection)
                    return;
                QScopedValueRollback<bool> guard(s->syncingSelection, true);
                const auto selected = s->tree->selection();
                s->treeOwnsSelection = !selected.empty();
                s->selected = selected.parts.isEmpty() ? QString() : selected.parts.front();
                s->selectedProbe = selected.parts.isEmpty() && selected.probes.size() == 1
                                       ? selected.probes.front()
                                       : QString();
                auto *canvas = s->activeView();
                auto *other = canvas == s->circuit ? s->schematic : s->circuit;
                other->selectParts({});
                canvas->selectParts(selected.parts);
                s->treeRemove->setEnabled(!s->busy() && !selected.empty());
                s->refreshProperties();
            });
    connect(s->tree, &CircuitTree::locatePin, this,
            [=](Pin pin)
            {
                if (const auto *part = s->doc.circuit.part(pin.part))
                    s->activeView()->centerOn(part->position);
            });
    connect(fit, &QToolButton::clicked, this,
            [=]
            {
                s->circuit->fit();
                s->schematic->fit();
                s->plot->resetZoom();
            });
    connect(split, &QToolButton::clicked, this, [=] { s->splitViews(); });
    connect(merge, &QToolButton::clicked, this, [=] { s->mergeViews(); });
    connect(analysis, &QToolButton::clicked, this, [=] { s->analysisDialog(); });
    connect(s->run, &QToolButton::clicked, this, [=] { s->toggleRun(); });
    connect(s->stop, &QToolButton::clicked, &s->simulator, &Simulator::stop);
    connect(s->reset, &QToolButton::clicked, this, [=] { s->resetRun(); });
    connect(s->value, &QLineEdit::editingFinished, this, [=] { s->applyProperty(false); });
    connect(s->initial, &QLineEdit::editingFinished, this, [=] { s->applyProperty(true); });
    connect(s->reference, &QLineEdit::editingFinished, this, [=] { s->renamePart(); });
    connect(filter, &QLineEdit::textChanged, s->tiles, &ComponentLibrary::filterComponents);
    connect(s->tiles, &ComponentLibrary::componentClicked, this,
            [=](QString kind)
            {
                s->circuit->place(kind);
                s->schematic->place(kind);
            });
    connect(s->tiles, &ComponentLibrary::componentDragStarted, this,
            [=]
            {
                s->setTool("select");
                select->setChecked(true);
            });
    for (auto *view : {s->circuit, s->schematic})
    {
        connect(view, &CircuitView::toolRequested, this,
                [=](QString tool)
                {
                    s->setTool(tool);
                    if (tool == "select")
                        select->setChecked(true);
                });
        connect(
            view, &CircuitView::labelPicked, this,
            [=](Pin pin)
            {
                if (s->busy())
                    return;
                bool ok;
                QString existing;
                for (const auto &label : s->doc.circuit.labels)
                    if (label.pin == pin)
                        existing = label.name;
                QString name = QInputDialog::getText(this, "Net label",
                                                     "Matching labels connect pins. Leave empty to remove.",
                                                     QLineEdit::Normal, existing, &ok);
                if (!ok)
                    return;
                if (!name.isEmpty() &&
                    !QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,31}$").match(name).hasMatch())
                {
                    s->message("Invalid net label name.");
                    return;
                }
                s->doc.edit("Set net label",
                            [=](Circuit &c)
                            {
                                c.labels.removeIf([&](const NetLabel &label) { return label.pin == pin; });
                                if (!name.isEmpty())
                                    c.labels.append({name, pin});
                            });
            });
        connect(view, &CircuitView::partSelected, this,
                [=](QString id)
                {
                    if (s->syncingSelection || !view->isVisible())
                        return;
                    if (view->hasFocus())
                        s->lastView = view;
                    if (view != s->activeView())
                        return;
                    s->treeOwnsSelection = false;
                    s->selectedProbe.clear();
                    s->selected = id;
                    QStringList ids;
                    for (auto *item : view->scene()->selectedItems())
                        if (item->data(0).isValid())
                            ids << item->data(0).toString();
                    s->tree->setSelectedParts(ids);
                    s->treeRemove->setEnabled(!s->busy() && !s->tree->selection().empty());
                    s->propertyError = false;
                    s->value->setStyleSheet({});
                    s->initial->setStyleSheet({});
                    s->refreshProperties();
                });
        connect(view, &CircuitView::notice, this, [=](QString text) { s->status->setText(text); });
        connect(view, &CircuitView::probePicked, this,
                [=](Pin pin)
                {
                    if (s->busy())
                        return;
                    bool ok;
                    QString name =
                        QInputDialog::getText(this, "Voltage probe", "Name (letters, digits, underscore)",
                                              QLineEdit::Normal, "probe", &ok);
                    if (ok && QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,31}$").match(name).hasMatch())
                        s->doc.edit("Add voltage probe", [=](Circuit &c) { c.probes[name] = pin; });
                });
    }
    s->undo = s->doc.undo.createUndoAction(this, "Undo");
    s->undo->setShortcut(QKeySequence::Undo);
    s->redo = s->doc.undo.createRedoAction(this, "Redo");
    s->redo->setShortcut(QKeySequence::Redo);
    auto *file = appMenu_->addMenu("&File");
    auto *newAction =
        file->addAction("New circuit", QKeySequence::New, this, [=] { s->loadExample(Circuit{}); });
    auto *open = file->addAction("Open…", QKeySequence::Open, this,
                                 [=]
                                 {
                                     if (s->busy() || !s->discard())
                                         return;
                                     QString path = QFileDialog::getOpenFileName(this, "Open circuit", {},
                                                                                 "CMP circuits (*.cmp)");
                                     if (path.isEmpty())
                                         return;
                                     QString error;
                                     if (!s->doc.load(path, error))
                                         QMessageBox::warning(this, "Open failed", error);
                                     else
                                     {
                                         s->circuit->fit();
                                         s->schematic->fit();
                                     }
                                 });
    file->addAction("Save", QKeySequence::Save, this, [=] { s->save(false); });
    file->addAction("Save As…", QKeySequence::SaveAs, this, [=] { s->save(true); });
    auto *recover = file->addAction("Recover unsaved work…", this, [=] { s->recoverWork(); });
    s->editingActions << recover;
    auto *examples = file->addMenu("Examples");
    s->editingActions << newAction << open;
    for (auto pair :
         QVector<QPair<QString, std::function<Circuit()>>>{{"555 blinker", Circuit::blinker},
                                                           {"RC charging", Circuit::rc},
                                                           {"Transistor switch", Circuit::transistor},
                                                           {"LM339 threshold detector", Circuit::comparator}})
        s->editingActions << examples->addAction(pair.first, this, [=] { s->loadExample(pair.second()); });
    file->addSeparator();
    file->addAction("Export waveform CSV…", this,
                    [=]
                    {
                        QString path = QFileDialog::getSaveFileName(this, "Export samples", "waveforms.csv",
                                                                    "CSV (*.csv)");
                        if (path.isEmpty())
                            return;
                        QString error;
                        if (!s->plot->exportCsv(path, error))
                            QMessageBox::warning(this, "Export failed", error);
                        else
                            s->message("Exported all raw samples to CSV.");
                    });
    file->addAction("Export SPICE netlist…", this,
                    [=]
                    {
                        QString path = QFileDialog::getSaveFileName(this, "Export netlist", "circuit.cir",
                                                                    "SPICE netlist (*.cir)");
                        if (path.isEmpty())
                            return;
                        QSaveFile f(path);
                        if (f.open(QIODevice::WriteOnly))
                        {
                            f.write(s->doc.circuit.netlist().toUtf8());
                            if (!f.commit())
                                QMessageBox::warning(this, "Export failed", f.errorString());
                        }
                        else
                            QMessageBox::warning(this, "Export failed", f.errorString());
                    });
    file->addSeparator();
    file->addAction("Exit", QKeySequence::Quit, this, &QWidget::close);
    auto *edit = appMenu_->addMenu("&Edit");
    edit->addAction(s->undo);
    edit->addAction(s->redo);
    edit->addAction("Place net label…", this,
                    [=]
                    {
                        s->setTool("label");
                        s->status->setText("Click a component pin to assign a net label.");
                    });
    edit->addSeparator();
    edit->addAction("Delete selection", QKeySequence::Delete, remove, &QToolButton::click);
    edit->addAction("Rotate selection", QKeySequence("R"), rotate, &QToolButton::click);
    auto *view = appMenu_->addMenu("&View");
    auto *appearance = view->addMenu("Appearance");
    auto *appearanceGroup = new QActionGroup(this);
    appearanceGroup->setExclusive(true);
    for (const auto &choice :
         {qMakePair(QString("Follow System"), QString("system")),
          qMakePair(QString("Light"), QString("light")), qMakePair(QString("Dark"), QString("dark"))})
    {
        auto *action = appearance->addAction(choice.first);
        action->setObjectName("appearance_" + choice.second);
        action->setData(choice.second);
        action->setCheckable(true);
        action->setChecked(appearance_ == choice.second);
        appearanceGroup->addAction(action);
    }
    connect(appearanceGroup, &QActionGroup::triggered, this,
            [this](QAction *action) { setAppearance(action->data().toString()); });
    view->addSeparator();
    view->addAction("Circuit view", this,
                    [=]
                    {
                        auto *target = s->left->indexOf(s->circuit) >= 0 ? s->left : s->right;
                        target->setCurrentWidget(s->circuit);
                    });
    view->addAction("Schematic view", this,
                    [=]
                    {
                        auto *target = s->left->indexOf(s->schematic) >= 0 ? s->left : s->right;
                        target->setCurrentWidget(s->schematic);
                    });
    view->addSeparator();
    view->addAction("Split views side by side", this, [=] { s->splitViews(); });
    view->addAction("Merge workspace tabs", this, [=] { s->mergeViews(); });
    view->addAction("Fit workspace", fit, &QToolButton::click);
    auto *simulation = appMenu_->addMenu("&Simulation");
    simulation->addAction("Run / Pause / Resume", s->run, &QToolButton::click);
    simulation->addAction("Stop", s->stop, &QToolButton::click);
    simulation->addAction("Reset", s->reset, &QToolButton::click);
    s->editingActions << simulation->addAction("Analysis settings…", this, [=] { s->analysisDialog(); });
    simulation->addAction("Validate circuit", this,
                          [=]
                          {
                              auto errors = s->doc.circuit.validate();
                              s->diagnostics->setPlainText(errors.isEmpty() ? "Circuit validation passed."
                                                                            : errors.join('\n'));
                              instruments->setCurrentIndex(1);
                          });
    auto *help = appMenu_->addMenu("&Help");
    help->addAction("Documentation", this, [this] { showDocumentation("README.md"); });
    help->addAction(
        "Quick start", this,
        [=]
        {
            QMessageBox::information(
                this, "Using CMP",
                "Click a component tile, then the workspace to place it.\nSelect and drag components; R "
                "rotates; Delete removes.\nConnect pins with the connector tool. Crossing wires do not "
                "connect.\nThe probe tool adds named voltage traces. Branch current traces are available in "
                "the waveform legend.\n\nRun/Pause share F5. Stop retains results; Reset clears them.\nClick "
                "a waveform to inspect readings; wheel zooms time.\nThe mouse wheel zooms a circuit "
                "view.\n\nCircuit view shows physical parts with explicit wired pins, not automatic "
                "breadboard hole connections.");
        });
    help->addAction(
        "Model limitations", this,
        [=]
        {
            QMessageBox::information(
                this, "Model coverage",
                "CMP uses original functional models with ngspice 47.\n\nNE555: threshold/trigger latch, "
                "reset, discharge and approximate output resistance. No detailed internal transistor, "
                "thermal, power or high-frequency model.\nDiode, LED and NPN: educational parameter models, "
                "not manufacturer-qualified models.\nCapacitors: ideal; no ESR, leakage or damage "
                "simulation.\nLM339: four open-collector channels, 40 ohm on/1 Tohm off paths; no internal "
                "pull-up, offset, delay or temperature accuracy. Inputs require the documented nominal "
                "common-mode range.\n\nVerify a physical build separately before claiming hardware "
                "correlation.");
        });
    connect(&s->doc, &Document::changed, this,
            [=]
            {
                if (!s->busy())
                {
                    s->plot->clear();
                    s->clock->setText("0.000 s");
                    s->cursorReadout->clear();
                    s->updateTraces();
                }
                if (!s->doc.circuit.part(s->selected))
                    s->selected.clear();
                if (!s->doc.circuit.probes.contains(s->selectedProbe))
                    s->selectedProbe.clear();
                s->treeRemove->setEnabled(!s->busy() && !s->tree->selection().empty());
                s->refreshProperties();
                s->updateTitle();
            });
    connect(&s->doc.undo, &QUndoStack::cleanChanged, this, [=] { s->updateTitle(); });
    connect(&s->simulator, &Simulator::message, this,
            [=](QString text) { s->diagnostics->appendPlainText(text); });
    connect(&s->simulator, &Simulator::stateChanged, this,
            [=](QString phase)
            {
                if (s->resetPending && phase != "Running")
                    return;
                s->setState(phase);
            });
    connect(&s->simulator, &Simulator::samplesReady, this,
            [=](Samples batch)
            {
                if (s->resetPending || s->phase == "Stopped" || s->phase == "Ready" || s->phase == "Failed")
                    return;
                s->plot->append(batch);
                if (!batch.isEmpty())
                {
                    auto &sample = batch.back();
                    s->clock->setText(QString::number(sample.time, 'f', 3) + " s");
                    s->readCursor(sample.time, sample);
                }
            });
    connect(&s->simulator, &Simulator::completed, this,
            [=](bool ok)
            {
                if (s->resetPending)
                {
                    s->resetPending = false;
                    s->setState("Ready");
                    return;
                }
                const auto report = s->simulator.hasElectricalReport()
                                        ? s->simulator.electricalReport()
                                        : diagnoseElectrical(s->doc.circuit, s->plot->samples);
                for (const auto &line : report.lines(ok))
                    s->diagnostics->appendPlainText(line);
                if (!report.issues.isEmpty() || !report.incomplete.isEmpty() || !ok)
                    instruments->setCurrentIndex(1);
                s->message(
                    QString("Solver %1 · %2 sample(s) · electrical: %3 violation(s), %4 incomplete check(s)")
                        .arg(ok ? "completed" : "incomplete")
                        .arg(s->plot->samples.size())
                        .arg(report.issues.size())
                        .arg(report.incomplete.size()));
            });
    connect(s->plot, &Waveform::cursorChanged, this,
            [=](double t, Sample sample) { s->readCursor(t, sample); });
    s->updateTraces();
    s->refreshProperties();
    s->setState("Ready");
    s->doc.undo.setClean();
    s->updateTitle();
    s->diagnostics->setPlainText(
        "ngspice 47 · CMP functional models v0.1\nLoad an example or build a circuit with explicit pin "
        "connections.\nCircuit and Schematic views share the same document.");
    appMenu_->ensurePolished();
    appMenu_->setFixedWidth(appMenu_->sizeHint().width());
    setupGlobalSearch();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme)
            {
                if (appearance_ == "system")
                    applyAppearance();
            });
    applyAppearance();
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    s->body->restoreState(settings.value("window/body").toByteArray());
    s->inspector->restoreState(settings.value("window/inspector").toByteArray());
    s->center->restoreState(settings.value("window/center").toByteArray());
    s->tree->setSelectedParts({s->selected});
    s->circuit->selectPart(s->selected);
    s->treeRemove->setEnabled(!s->tree->selection().empty());
    const auto arguments = QCoreApplication::arguments();
    const bool testRun = arguments.contains("--smoke") || arguments.contains("--capture") ||
                         arguments.contains("--close-running") || arguments.contains("--close-paused") ||
                         arguments.contains("--recovery-smoke");
    if (!testRun)
    {
        s->recovery = std::make_unique<Recovery>(
            &s->doc, QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery");
        connect(s->recovery.get(), &Recovery::error, this, [=](const QString &text) { s->message(text); });
        connect(qApp, &QGuiApplication::applicationStateChanged, this,
                [=](Qt::ApplicationState state)
                {
                    if (state != Qt::ApplicationActive)
                        s->recovery->flush();
                });
        QTimer::singleShot(200, this, [=] { s->recoverWork(true); });
    }
    QTimer::singleShot(0, this,
                       [=]
                       {
                           s->circuit->fit();
                           s->schematic->fit();
                       });
}
void MainWindow::setAppearance(const QString &mode)
{
    if (mode != "system" && mode != "light" && mode != "dark")
        return;
    appearance_ = mode;
    QSettings settings;
    settings.setValue("appearance/mode", mode);
    settings.sync();
    applyAppearance();
    if (settings.status() != QSettings::NoError)
        state->message("Appearance applied for this session; the preference could not be saved.");
}
void MainWindow::applyAppearance()
{
    auto *s = state.get();
    installTheme(appearanceIsDark(appearance_, QGuiApplication::styleHints()->colorScheme()));
    for (auto *action : findChildren<QAction *>())
        if (action->objectName().startsWith("appearance_"))
            action->setChecked(action->data().toString() == appearance_);
    for (auto *button : findChildren<QToolButton *>())
        if (button->objectName().startsWith("tool_"))
            button->setIcon(toolIcon(button->objectName().mid(5)));
    s->run->setIcon(toolIcon(s->phase == "Running" ? "pause" : "play"));
    s->treeRemove->setIcon(toolIcon("delete"));
    for (int index = 0; index < s->plot->traces.size(); ++index)
    {
        auto &trace = s->plot->traces[index];
        trace.color = theme().traces[index % theme().traces.size()];
        for (const auto &part : s->doc.circuit.parts)
            if (part.kind == "LED" && trace.key == "vi_" + part.id.toLower() + "#branch")
                trace.color = theme().good;
    }
    for (int index = 0; index < s->legend->count(); ++index)
        if (auto *label = qobject_cast<QCheckBox *>(s->legend->itemAt(index)->widget()))
        {
            const int trace = label->property("traceIndex").toInt();
            if (trace < s->plot->traces.size())
                label->setStyleSheet("QCheckBox{color:" + s->plot->traces[trace].color.name() +
                                     ";} QCheckBox:disabled{color:" + theme().disabled.name() + ";}");
        }
    QPixmap atlas(":/cmp/components.png");
    for (int index = 0; index < s->tiles->count(); ++index)
    {
        auto *item = s->tiles->item(index);
        if (item->data(Qt::UserRole).toString() != "GND")
            continue;
        const int sprite = spec("GND").sprite;
        const int x0 = sprite * atlas.width() / 5, x1 = (sprite + 1) * atlas.width() / 5;
        item->setIcon(QIcon(tintedPixmap(atlas.copy(x0, 0, x1 - x0, atlas.height() / 2), theme().text)));
    }
    auto *brand = header_->findChild<QLabel *>("brandIcon");
    QPixmap logo = QPixmap(":/cmp/logo.png").scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    brand->setPixmap(isDarkTheme() ? tintedPixmap(logo, theme().accent) : logo);
    s->circuit->refreshTheme();
    s->schematic->refreshTheme();
    s->plot->update();
    appMenu_->setFixedWidth(appMenu_->sizeHint().width());
    updateWindowTheme(this);
}
void MainWindow::showDocumentation(const QString &path, const QString &query)
{
    auto *dialog = new QDialog(this);
    dialog->setObjectName("documentationViewer");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("CMP documentation");
    dialog->resize(860, 680);
    auto *layout = new QVBoxLayout(dialog);
    auto *browser = new QTextBrowser;
    browser->setObjectName("documentationContent");
    browser->setOpenLinks(false);
    browser->setSearchPaths({":/help", ":/help/docs"});
    auto open = [browser](const QString &document)
    {
        if (QFile::exists(":/help/" + document))
            browser->setSource(QUrl("qrc:/help/" + document), QTextDocument::MarkdownResource);
        else
            browser->setPlainText("Documentation is not included in this build.");
    };
    connect(browser, &QTextBrowser::anchorClicked, dialog,
            [browser, open](const QUrl &url)
            {
                const auto target = browser->source().resolved(url);
                if (target.scheme() == "qrc" && target.path().startsWith("/help/"))
                    open(target.path().mid(6));
                else if (target.scheme() == "https" || target.scheme() == "http")
                    QDesktopServices::openUrl(target);
            });
    layout->addWidget(browser);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    open(path);
    if (!query.isEmpty())
        browser->find(query, QTextDocument::FindFlags{});
    dialog->show();
}
void MainWindow::setupGlobalSearch()
{
    auto *s = state.get();
    auto *search = qobject_cast<GlobalSearch *>(search_);
    QVector<SearchEntry> documentation;
    for (const auto &path :
         {QString("README.md"), QString("docs/FEATURES.md"), QString("docs/MVP-REQUIREMENTS.md"),
          QString("docs/IMPLEMENTATION-STATUS.md"), QString("docs/LIBRARY-EXPANSION.md"),
          QString("docs/LM339.md"), QString("docs/COMPONENT-LIBRARY.md")})
    {
        QFile file(":/help/" + path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto markdown = QString::fromUtf8(file.readAll());
        QString title, section, text;
        auto addSection = [&]
        {
            if (section.isEmpty())
                return;
            const QString heading = section;
            documentation.append({heading, "Documentation · " + title, text,
                                  [this, path, heading] { showDocumentation(path, heading); }});
        };
        for (const auto &line : markdown.split('\n'))
            if (line.startsWith('#'))
            {
                addSection();
                section = line;
                section.remove(QRegularExpression("^#+\\s*"));
                if (title.isEmpty())
                    title = section;
                text.clear();
            }
            else
                text += line + '\n';
        addSection();
    }
    search->entries = [this, s, documentation]
    {
        QVector<SearchEntry> entries;
        std::function<void(QMenu *, const QString &)> addMenu = [&](QMenu *menu, const QString &category)
        {
            for (auto *action : menu->actions())
            {
                QString title = action->text();
                title.remove('&');
                if (action->isSeparator() || !action->isVisible())
                    continue;
                if (action->menu())
                    addMenu(action->menu(), category + " / " + title);
                else
                {
                    QPointer<QAction> target(action);
                    entries.append({title, "Command · " + category, action->toolTip(),
                                    [target]
                                    {
                                        if (target && target->isEnabled())
                                            target->trigger();
                                    },
                                    action->isEnabled()});
                }
            }
        };
        for (auto *action : appMenu_->actions())
            if (action->menu())
            {
                QString category = action->text();
                category.remove('&');
                addMenu(action->menu(), category);
            }
        // Toolbar tools are searchable alongside menu commands.
        for (const auto &name : {"cursor", "wire", "probe"})
        {
            QPointer<QToolButton> button(findChild<QToolButton *>("tool_" + QString(name)));
            if (button)
                entries.append({button->accessibleName(), "Command · Tools", button->toolTip(),
                                [button]
                                {
                                    if (button && button->isEnabled())
                                        button->click();
                                },
                                button->isEnabled()});
        }
        for (const auto &component : catalog())
        {
            const auto kind = component.kind;
            entries.append({component.name, "Component · Place in workspace", kind,
                            [s, kind]
                            {
                                if (!s->busy())
                                {
                                    s->circuit->place(kind);
                                    s->schematic->place(kind);
                                }
                            },
                            !s->busy()});
        }
        for (const auto &part : s->doc.circuit.parts)
        {
            const auto id = part.id;
            entries.append({id + " · " + spec(part.kind).name, "Circuit part · Select and locate", part.kind,
                            [s, id]
                            {
                                if (const auto *part = s->doc.circuit.part(id))
                                {
                                    s->setTool("select");
                                    s->selectionTool->setChecked(true);
                                    auto *view = s->activeView();
                                    s->circuit->scene()->clearSelection();
                                    s->schematic->scene()->clearSelection();
                                    view->selectPart(id);
                                    view->centerOn(part->position);
                                    view->setFocus();
                                }
                            }});
        }
        entries += documentation;
        return entries;
    };
    auto *focus = new QAction("Global search", this);
    focus->setShortcut(QKeySequence("Ctrl+K"));
    addAction(focus);
    connect(focus, &QAction::triggered, this,
            [search]
            {
                search->setFocus();
                search->selectAll();
                search->refreshResults();
            });
}
MainWindow::~MainWindow()
{
    // Child views can clear selection while tearing down; stop callbacks before State is destroyed.
    for (auto *child : findChildren<QObject *>())
        disconnect(child, nullptr, this, nullptr);
    disconnect(&state->doc.undo, nullptr, this, nullptr);
}
void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    syncWindowsChrome();
    header_->updateCaptionSpace();
}
void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange)
    {
        syncWindowsChrome();
        if (header_)
            header_->updateCaptionSpace();
    }
}
void MainWindow::closeEvent(QCloseEvent *e)
{
    auto *s = state.get();
    if (!s->discard())
    {
        e->ignore();
        return;
    }
    s->simulator.stop();
    if (s->recovery)
        s->recovery->closeSession();
    if (QCoreApplication::arguments().contains("--close-running") ||
        QCoreApplication::arguments().contains("--close-paused"))
    {
        e->accept();
        return;
    }
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/body", s->body->saveState());
    settings.setValue("window/inspector", s->inspector->saveState());
    settings.setValue("window/center", s->center->saveState());
    e->accept();
}
void MainWindow::recoveryTest()
{
    auto *s = state.get();
    bool pass = true;
    auto check = [&](bool ok, const char *name)
    {
        pass &= ok;
        qInfo("%s %s", ok ? "PASS" : "FAIL", name);
    };
    QDir().mkpath("work");
    QTemporaryDir temp(QDir::current().filePath("work/recovery-ui-XXXXXX"));
    if (!temp.isValid())
    {
        QCoreApplication::exit(2);
        return;
    }
    const auto directory = temp.filePath("recovery");
    const auto original = temp.filePath("original.cmp");
    QString error, abandoned;
    {
        Document doc;
        doc.save(original, error);
        Recovery previous(&doc, directory);
        doc.edit("Unsaved edit", [](Circuit &c) { c.part("RA")->value = 44444; });
        check(previous.flush(), "UI recovery fixture snapshot");
        abandoned = previous.snapshotPath();
    }
    s->recovery = std::make_unique<Recovery>(&s->doc, directory);
    auto clickDialog = [=](const QString &button)
    {
        QTimer::singleShot(0, this,
                           [=]
                           {
                               auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                               auto *target = dialog ? dialog->findChild<QPushButton *>(button) : nullptr;
                               if (target)
                                   target->click();
                               else if (dialog)
                                   dialog->reject();
                           });
    };
    clickDialog("deferRecovery");
    s->recoverWork();
    check(QFile::exists(abandoned) && s->doc.undo.isClean(), "Later keeps snapshot and current document");
    clickDialog("recoverSnapshot");
    s->recoverWork();
    check(s->doc.circuit.part("RA")->value == 44444 && !s->doc.undo.isClean(), "Recovered work opens dirty");
    check(s->doc.path == original && !QFile::exists(abandoned) && QFile::exists(s->recovery->snapshotPath()),
          "Recovery transfers snapshot ownership");
    s->recovery->flush();
    QTimer::singleShot(0, this,
                       []
                       {
                           if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                               box->button(QMessageBox::Cancel)->click();
                       });
    check(!close() && QFile::exists(s->recovery->snapshotPath()), "Cancel close preserves unsaved recovery");
    Document disk;
    check(disk.load(original, error) && disk.circuit.part("RA")->value == 10000,
          "Recovery leaves original project unchanged");
    check(s->doc.save(original, error) && !QFile::exists(s->recovery->snapshotPath()),
          "Save after recovery removes snapshot");
    {
        Document doc;
        Recovery previous(&doc, directory);
        doc.edit("Discard fixture", [](Circuit &c) { c.part("RA")->value = 55555; });
        previous.flush();
        abandoned = previous.snapshotPath();
    }
    clickDialog("discardSnapshot");
    s->recoverWork();
    check(!QFile::exists(abandoned) && s->doc.undo.isClean() && s->doc.circuit.part("RA")->value == 44444,
          "Discard selected removes abandoned snapshot only");
    s->recovery->closeSession();
    s->recovery.reset();
    QCoreApplication::exit(pass ? 0 : 1);
}
void MainWindow::lifecycleTest(bool paused)
{
    auto *s = state.get();
    for (int i = 0; i < 25; ++i)
    {
        s->toggleRun();
        s->simulator.stop();
        s->resetRun();
        if (s->phase != "Ready" || !s->plot->samples.isEmpty())
        {
            QCoreApplication::exit(2);
            return;
        }
    }
    s->toggleRun();
    auto *timer = new QTimer(this);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    connect(timer, &QTimer::timeout, this,
            [=]
            {
                if (s->phase == "Running")
                {
                    timer->stop();
                    if (paused)
                        s->simulator.pause();
                    QTimer::singleShot(50, this, [this] { close(); });
                }
                else if (elapsed->elapsed() > 5000)
                {
                    timer->stop();
                    QCoreApplication::exit(3);
                }
            });
    timer->start(20);
}
void MainWindow::smokeTest()
{
    auto *s = state.get();
    const QString originalAppearance = appearance_;
    const QVariant savedAppearance = QSettings().value("appearance/mode");
    QDir().mkpath("work");
    QFile("work/ui-smoke-details.txt").remove();
    auto result = std::make_shared<bool>(true);
    auto check = [result](bool pass, const QString &name)
    {
        *result &= pass;
        QFile f("work/ui-smoke-details.txt");
        f.open(QIODevice::WriteOnly | QIODevice::Append);
        QTextStream out(&f);
        out << (pass ? "PASS " : "FAIL ") << name << "\n";
    };
    check(s->left->count() == 2 && s->doc.circuit.validate().isEmpty(), "initial fixture");
    {
        QTimer::singleShot(
            0, this,
            [=]
            {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                auto *rating = dialog ? dialog->findChild<QLineEdit *>("rating_voltage") : nullptr;
                auto *buttons = dialog ? dialog->findChild<QDialogButtonBox *>() : nullptr;
                check(rating && buttons, "electrical ratings dialog opens for capacitor");
                if (!rating || !buttons)
                {
                    if (dialog)
                        dialog->reject();
                    return;
                }
                rating->setText("-1");
                buttons->button(QDialogButtonBox::Save)->click();
                check(dialog->isVisible() && s->doc.circuit.part("C1")->ratings.value("voltage") == 25,
                      "invalid electrical rating stays in editor without document mutation");
                rating->setText("50");
                buttons->button(QDialogButtonBox::Save)->click();
            });
        s->ratingsButton->click();
        check(s->doc.circuit.part("C1")->ratings.value("voltage") == 50, "electrical rating editor commits");
        s->doc.undo.undo();
        check(s->doc.circuit.part("C1")->ratings.value("voltage") == 25, "electrical rating editor undo");
        s->doc.undo.clear();
    }

    {
        auto *shortcut = search_->findChild<QLabel *>("globalSearchShortcut");
        search_->clear();
        bool aligned = shortcut && shortcut->geometry().right() >= search_->width() - 12 &&
                       !search_->placeholderText().contains("Ctrl");
        search_->setText("resistor");
        for (auto *button : search_->findChildren<QAbstractButton *>())
            if (button->isVisible())
                aligned &=
                    !QRect(button->mapTo(search_, QPoint()), button->size()).intersects(shortcut->geometry());
        search_->clear();
        aligned &= shortcut->geometry().right() >= search_->width() - 12;
        check(aligned, "global search shortcut is right-aligned independently of query and clear button");
    }
    {
        const auto original = s->doc.circuit.json();
        auto row = [&](const QString &kind, const QString &id) -> QTreeWidgetItem *
        {
            for (QTreeWidgetItemIterator it(s->tree); *it; ++it)
                if ((*it)->data(0, Qt::UserRole).toString() == kind &&
                    (*it)->data(0, Qt::UserRole + 1).toString() == id)
                    return *it;
            return nullptr;
        };
        check(s->tree->topLevelItem(0)->childCount() == s->doc.circuit.parts.size() &&
                  s->tree->topLevelItem(1)->childCount() == s->doc.circuit.probes.size(),
              "circuit tree lists all components and named voltage probes");
        s->tree->setCurrentItem(row("part", "RA"));
        check(s->selected == "RA" && s->reference->text() == "RA" && !s->reference->isReadOnly() &&
                  s->circuit->scene()->selectedItems().size() == 1,
              "tree component selection updates viewport and properties");
        s->circuit->selectPart("RB");
        check(s->tree->selection().parts == QStringList{"RB"},
              "viewport selection synchronizes the circuit tree");
        s->tree->setCurrentItem(row("probe", "timing"));
        check(s->selectedProbe == "timing" && s->type->text() == "Voltage probe" &&
                  s->reference->isReadOnly() && s->pins->text().contains("C1"),
              "tree probe selection identifies its attached component pin");
        QKeyEvent removeProbe(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(s->tree, &removeProbe);
        bool traceGone = true;
        for (const auto &trace : s->plot->traces)
            traceGone &= trace.name != "V(timing)";
        check(!s->doc.circuit.probes.contains("timing") && s->doc.circuit.part("C1") && traceGone,
              "Delete from probe tree removes only the probe and its waveform trace");
        s->doc.undo.undo();
        check(s->doc.circuit.json() == original && row("probe", "timing"),
              "undo restores deleted probe and tree row");
        s->tree->setCurrentItem(row("part", "RA"));
        row("part", "RB")->setSelected(true);
        check(s->circuit->scene()->selectedItems().size() == 2, "tree supports multiple selected components");
        s->treeRemove->click();
        bool attachedWiresGone = true;
        for (const auto &wire : s->doc.circuit.wires)
            attachedWiresGone &=
                wire.a.part != "RA" && wire.b.part != "RA" && wire.a.part != "RB" && wire.b.part != "RB";
        check(!s->doc.circuit.part("RA") && !s->doc.circuit.part("RB") && attachedWiresGone,
              "tree delete button removes selected components and their connected wires");
        s->doc.undo.undo();
        check(s->doc.circuit.json() == original, "multi-component deletion is one undoable edit");
        s->tree->setCurrentItem(row("part", "C1"));
        row("probe", "out")->setSelected(true);
        auto clickCanvas = [&](QPoint point)
        {
            auto *target = s->circuit->viewport();
            QMouseEvent press(QEvent::MouseButtonPress, point, target->mapToGlobal(point), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, point, target->mapToGlobal(point), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(target, &release);
        };
        clickCanvas(s->circuit->mapFromScene(s->doc.circuit.part("C1")->position));
        check(s->tree->selection().probes.isEmpty() && s->tree->selection().parts == QStringList{"C1"} &&
                  !s->treeOwnsSelection,
              "clicking an already-selected component clears mixed tree selection ownership");
        row("probe", "out")->setSelected(true);
        s->deleteSelection();
        check(!s->doc.circuit.part("C1") && !s->doc.circuit.probes.contains("timing") &&
                  !s->doc.circuit.probes.contains("out") && s->doc.circuit.probes.contains("vcc"),
              "shared delete action handles mixed component and probe selection with attached-probe cleanup");
        s->doc.undo.undo();
        check(s->doc.circuit.json() == original, "undo restores mixed deletion and connections");
        s->tree->setCurrentItem(row("probe", "timing"));
        clickCanvas({12, 12});
        check(s->tree->selection().empty() && s->selectedProbe.isEmpty(),
              "empty canvas click clears a probe-only tree selection");
        s->tree->setCurrentItem(row("probe", "timing"));
        s->doc.editsLocked = true;
        s->tree->deleteSelection();
        check(s->doc.circuit.json() == original, "circuit-tree deletion respects the simulation edit lock");
        s->doc.editsLocked = false;
        s->tree->clearSelection();
        s->circuit->selectPart("C1");
    }
    check(s->circuit->toolTip().isEmpty() && s->schematic->toolTip().isEmpty() &&
              s->circuit->viewport()->toolTip().isEmpty(),
          "empty canvas has no navigation tooltip");
    {
        auto resistorImage = [&]
        {
            QImage image(172, 172, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.scale(2, 2);
            painter.translate(43, 43);
            for (auto *item : s->circuit->scene()->items())
                if (item->data(0).toString() == "RA")
                {
                    QStyleOptionGraphicsItem option;
                    item->paint(&painter, &option);
                }
            return image;
        };
        const auto before = resistorImage();
        s->doc.edit("Test resistor colours", [](Circuit &c) { c.part("RA")->value = 4700; });
        const auto changed = resistorImage();
        bool bandTooltip = false;
        for (auto *item : s->circuit->scene()->items())
            if (item->data(0).toString() == "RA")
                bandTooltip = item->toolTip().contains("yellow / violet / red / gold");
        s->doc.undo.undo();
        check(before != changed && before == resistorImage() && bandTooltip,
              "resistor value edits repaint the actual bands and undo restores the artwork");
        before.save("work/resistor-10k.png");
        changed.save("work/resistor-4k7.png");
    }
    {
        const auto original = s->doc.circuit.json();
        QHash<QString, QColor> colors;
        bool endpoints = true, orthogonal = true, consistentColors = true;
        QGraphicsItem *hoverWire = nullptr;
        for (auto *item : s->circuit->scene()->items())
            if (item->data(1).isValid())
            {
                auto *wire = dynamic_cast<QGraphicsPathItem *>(item);
                const int index = item->data(1).toInt();
                const auto &connection = s->doc.circuit.wires[index];
                const auto path = wire->path();
                const auto first = path.elementAt(0), last = path.elementAt(path.elementCount() - 1);
                endpoints &=
                    QLineF(QPointF(first.x, first.y),
                           pinPosition(*s->doc.circuit.part(connection.a.part), connection.a.index, true))
                        .length() < .001;
                endpoints &=
                    QLineF(QPointF(last.x, last.y),
                           pinPosition(*s->doc.circuit.part(connection.b.part), connection.b.index, true))
                        .length() < .001;
                for (int i = 1; i < path.elementCount(); ++i)
                {
                    auto a = path.elementAt(i - 1), b = path.elementAt(i);
                    orthogonal &= std::abs(a.x - b.x) < .001 || std::abs(a.y - b.y) < .001;
                }
                const auto net = item->data(2).toString();
                if (colors.contains(net))
                    consistentColors &= colors[net] == wire->pen().color();
                colors[net] = wire->pen().color();
                hoverWire = item;
            }
        check(endpoints && orthogonal && consistentColors && colors.size() > 3,
              "routed wires retain exact terminals orthogonal segments and consistent net colors");
        if (hoverWire)
        {
            auto *canvas = s->circuit;
            auto *target = canvas->viewport();
            QPoint wirePoint;
            bool found = false;
            auto path = static_cast<QGraphicsPathItem *>(hoverWire)->path();
            for (int step = 1; step < 100 && !found; ++step)
            {
                const QPoint point = canvas->mapFromScene(path.pointAtPercent(step / 100.));
                for (auto *item : canvas->items(point))
                {
                    if (item->data(3).toBool())
                        continue;
                    found =
                        item == hoverWire && target->rect().adjusted(100, 100, -100, -100).contains(point);
                    break;
                }
                if (found)
                    wirePoint = point;
            }
            auto mouse =
                [&](QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons buttons)
            {
                QMouseEvent event(type, point, target->mapToGlobal(point), button, buttons, Qt::NoModifier);
                QApplication::sendEvent(target, &event);
            };
            mouse(QEvent::MouseMove, wirePoint, Qt::NoButton, Qt::NoButton);
            check(s->status->text().contains(" pin ") && s->status->text().contains("↔") &&
                      s->doc.circuit.json() == original && found &&
                      static_cast<QGraphicsPathItem *>(hoverWire)->pen().widthF() > 3,
                  "wire hover identifies both endpoints without editing connectivity");
            s->circuit->grab().save("work/wire-highlight.png");
            bool panHover = found;
            for (auto button : {Qt::RightButton, Qt::MiddleButton, Qt::LeftButton})
            {
                QPoint empty;
                bool emptyFound = false;
                for (QPoint delta : {QPoint(50, 30), QPoint(-50, 30), QPoint(30, -50), QPoint(-30, -50)})
                {
                    const QPoint point = wirePoint + delta;
                    if (canvas->items(point).isEmpty() &&
                        target->rect().adjusted(10, 10, -10, -10).contains(point + delta))
                    {
                        empty = point;
                        emptyFound = true;
                        break;
                    }
                }
                panHover &= emptyFound;
                if (!emptyFound)
                    break;
                mouse(QEvent::MouseMove, empty, Qt::NoButton, Qt::NoButton);
                int hoverNotices = 0;
                auto connection =
                    connect(canvas, &CircuitView::notice, this, [&](QString) { ++hoverNotices; });
                if (button == Qt::LeftButton)
                {
                    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
                    QApplication::sendEvent(canvas, &space);
                }
                mouse(QEvent::MouseButtonPress, empty, button, button);
                // Bring the wire underneath the original empty drag origin.
                mouse(QEvent::MouseMove, empty + (empty - wirePoint), Qt::NoButton, button);
                QCoreApplication::processEvents();
                QGraphicsSceneHoverEvent stale(QEvent::GraphicsSceneHoverEnter);
                canvas->scene()->sendEvent(hoverWire, &stale);
                panHover &= hoverNotices == 0;
                for (auto *item : canvas->scene()->items())
                    if (item->data(1).isValid())
                        panHover &= static_cast<QGraphicsPathItem *>(item)->pen().widthF() == 2 &&
                                    item->opacity() == 1;
                // Use the original known exposed point, translated with the camera.
                const QPoint currentWire = empty;
                mouse(QEvent::MouseButtonRelease, currentWire, button, Qt::NoButton);
                panHover &= static_cast<QGraphicsPathItem *>(hoverWire)->pen().widthF() > 3;
                disconnect(connection);
                if (button == Qt::LeftButton)
                {
                    QKeyEvent space(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
                    QApplication::sendEvent(canvas, &space);
                }
                canvas->fit();
            }
            check(
                panHover && s->doc.circuit.json() == original,
                "pan ignores stale scene hover at the drag origin and release hit-tests the current cursor");
            mouse(QEvent::MouseMove, {5, 5}, Qt::NoButton, Qt::NoButton);
        }
    }
    {
        bool fullHeight = true;
        for (const auto &name : {"captionMinimize", "captionMaximize", "captionClose"})
            if (auto *button = header_->findChild<QToolButton *>(name))
                fullHeight &=
                    button->mapTo(header_, QPoint()).y() == 0 && button->height() == header_->height();
        check(fullHeight, "caption controls occupy full title-bar height");
        auto *separator = header_->findChild<QFrame *>("titleMenuSeparator");
        check(appMenu_->width() == appMenu_->sizeHint().width() && separator &&
                  s->project->x() > separator->x() && search_->isVisible() &&
                  search_->x() - (s->project->x() + s->project->width()) > 80,
              "compact menus separator project name and search leave a window-drag region");
        auto *search = qobject_cast<GlobalSearch *>(search_);
        auto activate = [&](const QString &query, const QString &prefix)
        {
            search->setText(query);
            search->refreshResults();
            auto *model = search->completer()->completionModel();
            for (int row = 0; row < model->rowCount(); ++row)
                if (model->index(row, 0).data().toString().startsWith(prefix))
                {
                    search->completer()->activated(model->index(row, 0));
                    QCoreApplication::processEvents();
                    return true;
                }
            return false;
        };
        check(activate("Schematic view", "Schematic view") && s->schematic->isVisible(),
              "global search executes a menu command");
        QEventLoop clearButtonSettled;
        QTimer::singleShot(250, &clearButtonSettled, &QEventLoop::quit);
        clearButtonSettled.exec();
        bool clearHidden = search->text().isEmpty();
        for (auto *button : search->findChildren<QAbstractButton *>())
            clearHidden &= !button->isVisible();
        check(clearHidden, "search result activation clears the query and hides the native clear button");
        activate("Circuit view", "Circuit view");
        const int parts = s->doc.circuit.parts.size();
        check(activate("NE555", "NE555 timer") && s->circuit->cursor().shape() == Qt::CrossCursor &&
                  s->doc.circuit.parts.size() == parts,
              "global component search enters placement without adding a part");
        s->setTool("select");
        check(activate("C1", "C1 ·") && s->reference->text() == "C1",
              "global circuit-part search selects the existing part");
        if (QFile::exists(":/help/docs/FEATURES.md"))
        {
            check(activate("CMP feature tracker", "CMP feature tracker"),
                  "global search indexes bundled documentation");
            auto *viewer = findChild<QDialog *>("documentationViewer");
            auto *content = viewer ? viewer->findChild<QTextBrowser *>("documentationContent") : nullptr;
            check(content && content->toPlainText().contains("Workspace and navigation"),
                  "documentation search opens readable offline Markdown");
            if (viewer)
                viewer->close();
        }
        else
        {
            showDocumentation("README.md");
            auto *viewer = findChild<QDialog *>("documentationViewer");
            auto *content = viewer ? viewer->findChild<QTextBrowser *>("documentationContent") : nullptr;
            check(content && !content->toPlainText().isEmpty(),
                  "code-only build explains unavailable documentation");
            if (viewer)
                viewer->close();
        }
        search->setText("zzzz_no_such_component");
        search->refreshResults();
        auto *model = search->completer()->completionModel();
        check(model->rowCount() == 1 && !(model->flags(model->index(0, 0)) & Qt::ItemIsEnabled),
              "global search exposes a disabled empty-result row");
        search->clear();
        s->circuit->fit();
    }
    {
        ComponentLibrary library(this);
        library.setViewMode(QListView::IconMode);
        library.setResizeMode(QListView::Fixed);
        library.setMovement(QListView::Static);
        library.setWrapping(true);
        library.setSpacing(0);
        library.setStyleSheet(s->tiles->styleSheet());
        library.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        for (int i = 0; i < s->tiles->count(); ++i)
            library.addItem(s->tiles->item(i)->clone());
        library.resize(350, 400);
        library.show();
        QCoreApplication::processEvents();
        const int gutter = library.width() - library.viewport()->width() + 2 * library.frameWidth() + 1;
        auto rowColumns = [&](int available)
        {
            library.resize(available + gutter, 400);
            QCoreApplication::processEvents();
            int count = 0;
            int first = 0;
            while (first < library.count() &&
                   library.item(first)->data(ComponentLibrary::HeaderRole).toBool())
                ++first;
            const int top = library.visualItemRect(library.item(first)).top();
            for (int i = 0; i < library.count(); ++i)
                count += !library.item(i)->data(ComponentLibrary::HeaderRole).toBool() &&
                         library.visualItemRect(library.item(i)).top() == top;
            return count;
        };
        bool stable = rowColumns(176) == 2;
        for (int i = 0; i < 12; ++i)
            stable &= rowColumns(i % 2 ? 286 : 260) == 2;
        stable &= rowColumns(288) == 3;
        for (int i = 0; i < 12; ++i)
            stable &= rowColumns(i % 2 ? 286 : 264) == 3;
        stable &= rowColumns(263) == 2;
        stable &= rowColumns(287) == 2;
        stable &= rowColumns(288) == 3;
        check(stable, "component columns use separate grow/shrink thresholds without breakpoint oscillation");
    }
    {
        class NavigationHarness final : public CircuitView
        {
          public:
            using CircuitView::CircuitView;
            QVector<QPoint> warps;
            std::optional<QPoint> livePosition;

          protected:
            QPoint panCursorPosition(QMouseEvent *event) const override
            {
                return livePosition.value_or(event->pos());
            }
            bool warpCursor(QPoint point) override
            {
                warps.append(point);
                return true;
            }
        };
        NavigationHarness view(&s->doc, true, this);
        view.resize(620, 420);
        view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view.show();
        QCoreApplication::processEvents();
        view.fit();
        const auto original = s->doc.circuit.json();
        auto *target = view.viewport();
        QPoint middle = target->rect().center();
        double initialZoom = view.transform().m11();
        auto wheel = [&](int amount)
        {
            QWheelEvent e(middle, target->mapToGlobal(middle), {}, QPoint(0, amount), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(target, &e);
        };
        wheel(120);
        check(view.transform().m11() > initialZoom, "wheel zooms in without Ctrl");
        wheel(-120);
        check(std::abs(view.transform().m11() - initialZoom) < 1e-9, "wheel zooms out without Ctrl");
        view.fit();
        auto mouse = [&](QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons buttons)
        {
            QMouseEvent e(type, p, target->mapToGlobal(p), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(target, &e);
        };
        auto move = [&](QPoint p) { mouse(QEvent::MouseMove, p, Qt::NoButton, Qt::RightButton); };
        mouse(QEvent::MouseButtonPress, middle, Qt::RightButton, Qt::RightButton);
        view.setSceneRect(view.mapToScene(target->rect()).boundingRect());
        double originalExtent = view.sceneRect().width();
        QPointF initialCenter = view.mapToScene(middle);
        bool stable = true, inside = true;
        QPoint latest = middle;
        for (int i = 0; i < 24; i++)
        {
            int old = view.warps.size();
            move({target->width() + 5, middle.y()});
            stable &= view.warps.size() == old + 1;
            if (view.warps.isEmpty())
                break;
            latest = view.warps.back();
            inside &= target->rect().contains(latest);
            QPointF beforeEcho = view.mapToScene(middle);
            move(latest);
            stable &= QLineF(beforeEcho, view.mapToScene(middle)).length() < 1e-6;
            latest += QPoint(20, 0);
            move(latest);
        }
        check(stable && inside && view.warps.size() == 24,
              "repeated edge wraps stay inside viewport without pan jumps");
        check(view.sceneRect().width() > originalExtent &&
                  QLineF(initialCenter, view.mapToScene(middle)).length() > 5000,
              "panning extends beyond the original canvas bounds");
        for (QPoint edge : {QPoint(-5, middle.y()), QPoint(middle.x(), -5),
                            QPoint(middle.x(), target->height() + 5), QPoint(-5, -5)})
        {
            int n = view.warps.size();
            move(edge);
            inside &= view.warps.size() == n + 1;
            if (!view.warps.isEmpty())
            {
                latest = view.warps.back();
                inside &= target->rect().contains(latest);
                move(latest);
            }
        }
        check(inside, "cursor wraps on left top bottom and corner edges");
        bool verticalStable = true;
        move(middle);
        const double verticalStartX = view.mapToScene(middle).x();
        for (int i = 0; i < 100; ++i)
        {
            QPoint edge(middle.x(), i % 2 ? -2 : target->height() + 2);
            view.livePosition = edge;
            move(edge);
            latest = view.warps.back();
            verticalStable &= target->rect().contains(latest) && latest.x() == middle.x();
            view.livePosition = latest;
            QPointF beforeStale = view.mapToScene(middle);
            move(edge); // Simulate a queued pre-warp event after the cursor has jumped.
            verticalStable &= QLineF(beforeStale, view.mapToScene(middle)).length() < 1e-6;
            verticalStable &= std::abs(view.mapToScene(middle).x() - verticalStartX) < 1e-6;
        }
        view.livePosition.reset();
        check(verticalStable, "vertical reversals ignore stale warp events without horizontal drift");
        mouse(QEvent::MouseButtonRelease, latest, Qt::RightButton, Qt::NoButton);
        int released = view.warps.size();
        mouse(QEvent::MouseMove, {target->width() + 5, middle.y()}, Qt::NoButton, Qt::NoButton);
        check(view.warps.size() == released, "cursor wrapping stops after pan release");
        mouse(QEvent::MouseButtonPress, middle, Qt::RightButton, Qt::RightButton);
        QFocusEvent out(QEvent::FocusOut);
        QApplication::sendEvent(&view, &out);
        move({target->width() + 5, middle.y()});
        check(view.warps.size() == released, "cursor wrapping stops on focus loss");
        bool slowPan = true;
        for (int width : {620, 621})
        {
            view.resize(width, 420);
            QCoreApplication::processEvents();
            middle = target->rect().center();
            for (double zoom : {.15, .37, .83, 1.13, 2.7})
            {
                view.resetTransform();
                view.scale(zoom, zoom);
                mouse(QEvent::MouseButtonPress, middle, Qt::RightButton, Qt::RightButton);
                QPoint current = middle + QPoint(20, 20);
                move(current); // Pass the initial drag threshold.
                const QPointF anchor = view.mapToScene(middle);
                QPoint previous = view.mapFromScene(anchor);
                for (QPoint direction :
                     {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1), QPoint(1, 1), QPoint(-1, -1)})
                    for (int step = 0; step < 40; ++step)
                    {
                        current += direction;
                        move(current);
                        QPoint now = view.mapFromScene(anchor);
                        slowPan &= now - previous == direction;
                        previous = now;
                    }
                mouse(QEvent::MouseButtonRelease, current, Qt::RightButton, Qt::NoButton);
            }
        }
        check(slowPan, "one-pixel slow pans track both axes exactly at fractional zoom and odd/even sizes");
        check(s->doc.circuit.json() == original, "zoom and continuous pan leave circuit data unchanged");
    }
    QFile anchorsFile(":/cmp/physical-pins.json");
    anchorsFile.open(QIODevice::ReadOnly);
    auto artwork = QJsonDocument::fromJson(anchorsFile.readAll()).object();
    QImage image(":/cmp/components.png");
    bool aligned = true;
    for (const auto &part : catalog())
    {
        if (expanded(part.kind))
            continue;
        auto points = artwork["pins"].toObject()[part.kind].toArray();
        aligned &= points.size() == part.pins.size();
        for (auto point : points)
        {
            auto uv = point.toArray();
            if (part.sprite < 0)
            {
                continue;
            }
            int x0 = (part.sprite % 5) * image.width() / 5, x1 = (part.sprite % 5 + 1) * image.width() / 5,
                y0 = (part.sprite / 5) * image.height() / 2, y1 = (part.sprite / 5 + 1) * image.height() / 2;
            int x = x0 + qRound(uv[0].toDouble() * (x1 - x0)), y = y0 + qRound(uv[1].toDouble() * (y1 - y0));
            aligned &= image.pixelColor(x, y).alpha() > 128;
        }
    }
    check(aligned, "all physical pin anchors overlap artwork terminals");
    {
        bool listed = false;
        for (int row = 0; row < s->tiles->count(); ++row)
            if (s->tiles->item(row)->data(Qt::UserRole).toString() == "LM339")
                listed = !s->tiles->item(row)->icon().isNull();
        check(listed, "LM339 catalog tile has native package artwork");
        for (bool physical : {true, false})
        {
            Document doc;
            Circuit c;
            c.parts = {{"U1", "LM339", 0, 0, {0, 0}, 0, {}, {}}};
            c.probes = {{"out1", {"U1", 0}}, {"out2", {"U1", 1}}, {"out3", {"U1", 13}}, {"out4", {"U1", 12}}};
            doc.replace(c);
            CircuitView view(&doc, physical, this);
            view.setGeometry(50, 100, 800, 600);
            view.show();
            view.setTool("probe");
            Pin picked{"", -1};
            connect(&view, &CircuitView::probePicked, this, [&](Pin pin) { picked = pin; });
            bool correct = true;
            for (int rotation : {0, 90, 180, 270})
            {
                doc.circuit.part("U1")->rotation = rotation;
                emit doc.changed();
                view.fit();
                QTransform packageTransform;
                packageTransform.rotate(rotation);
                const auto body =
                    packageTransform.mapRect(physical ? dipBodyRect(14) : QRectF(-95, -105, 190, 210));
                int callouts = 0;
                for (auto *item : view.scene()->items())
                    if (item->data(5).toString() == "probe")
                    {
                        ++callouts;
                        correct &= !item->sceneBoundingRect().intersects(body);
                    }
                correct &= callouts == 4;
                for (int index = 0; index < 14; ++index)
                {
                    picked = {"", -1};
                    const auto pos = pinPosition(*doc.circuit.part("U1"), index, physical);
                    if (physical)
                    {
                        QTransform transform;
                        transform.rotate(rotation);
                        correct &= QLineF(pos, transform.map(dipPinPosition(index, 14))).length() < .001;
                    }
                    const auto local = view.mapFromScene(pos);
                    const auto global = view.viewport()->mapToGlobal(local);
                    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                                      Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton,
                                        Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(view.viewport(), &press);
                    QApplication::sendEvent(view.viewport(), &release);
                    QApplication::processEvents();
                    correct &= picked == Pin{"U1", index};
                }
            }
            check(correct, physical ? "all 14 LM339 physical pins target correctly and probe tags clear its "
                                      "body at all four rotations"
                                    : "all 14 LM339 schematic pins target correctly and probe tags clear its "
                                      "body at all four rotations");
            doc.circuit.part("U1")->rotation = 0;
            emit doc.changed();
            view.fit();
            view.grab().save(physical ? "work/lm339-package.png" : "work/lm339-symbol.png");
            if (physical)
            {
                view.setTool("select");
                QScopedPointer<QMimeData> mime(s->tiles->mimeDataForKind("LM339"));
                const QPoint drop(650, 450);
                QDragEnterEvent enter(drop, Qt::CopyAction, mime.data(), Qt::LeftButton, Qt::NoModifier);
                QDropEvent event(drop, Qt::CopyAction, mime.data(), Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(view.viewport(), &enter);
                QApplication::sendEvent(view.viewport(), &event);
                check(event.isAccepted() && doc.circuit.parts.size() == 2 && doc.circuit.part("U2") &&
                          doc.circuit.part("U2")->kind == "LM339" && view.cursor().shape() == Qt::ArrowCursor,
                      "LM339 library drag drops one complete package and returns to selection");
                doc.undo.undo();
            }

            view.hide();
        }
        activateWindow();
        s->circuit->setFocus();
    }

    auto pan = [&](CircuitView *view, Qt::MouseButton button, bool space, QPoint local)
    {
        auto before = s->doc.circuit.json();
        QPointF centerBefore = view->mapToScene(view->viewport()->rect().center());
        if (space)
        {
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
            QApplication::sendEvent(view, &down);
        }
        QPoint to = local + QPoint(90, 45);
        QPoint global = view->viewport()->mapToGlobal(local), globalTo = view->viewport()->mapToGlobal(to);
        QMouseEvent press(QEvent::MouseButtonPress, local, global, button, button, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, to, globalTo, Qt::NoButton, button, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, to, globalTo, button, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &release);
        if (space)
        {
            QKeyEvent up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
            QApplication::sendEvent(view, &up);
        }
        bool moved = QLineF(centerBefore, view->mapToScene(view->viewport()->rect().center())).length() > 20;
        check(moved && before == s->doc.circuit.json(), space ? "Space-drag pans without moving parts"
                                                        : button == Qt::MiddleButton
                                                            ? "middle-drag pans without moving parts"
                                                            : "right-drag pans without moving parts");
        view->fit();
    };
    s->circuit->fit();
    pan(s->circuit, Qt::MiddleButton, false, s->circuit->viewport()->rect().center());
    pan(s->circuit, Qt::LeftButton, true, s->circuit->viewport()->rect().center());
    s->circuit->selectPart("C1");
    int contextRequests = 0;
    auto contextConnection =
        connect(s->circuit, &CircuitView::contextRequested, this, [&](QPointF) { ++contextRequests; });
    pan(s->circuit, Qt::RightButton, false, s->circuit->viewport()->rect().center());
    check(contextRequests == 0 && s->selected == "C1" && !s->circuit->scene()->selectedItems().isEmpty(),
          "right-drag preserves selection and suppresses context");
    {
        QPoint pos = s->circuit->viewport()->rect().center();
        QPoint global = s->circuit->viewport()->mapToGlobal(pos);
        QMouseEvent down(QEvent::MouseButtonPress, pos, global, Qt::RightButton, Qt::RightButton,
                         Qt::NoModifier);
        QApplication::sendEvent(s->circuit->viewport(), &down);
        QMouseEvent up(QEvent::MouseButtonRelease, pos, global, Qt::RightButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(s->circuit->viewport(), &up);
    }
    check(contextRequests == 1 && s->selected == "C1", "stationary right-click reserves one context request");
    disconnect(contextConnection);
    auto click = [](CircuitView *view, QPointF scenePoint)
    {
        QPoint local = view->mapFromScene(scenePoint);
        QPoint global = view->viewport()->mapToGlobal(local);
        QMouseEvent down(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &down);
        QMouseEvent up(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &up);
        QCoreApplication::processEvents(); // Smoke harness only: deliver the deferred pin edit.
    };
    bool iconsVisible = true;
    for (const auto &name : {"cursor", "wire", "probe", "rotate", "delete", "fit", "split", "merge",
                             "settings", "play", "pause", "stop", "reset"})
        iconsVisible &= !toolIcon(name).pixmap(20, 20).isNull();
    check(iconsVisible, "all Fluent tool icons render");
    int libraryParts = s->doc.circuit.parts.size();
    QListWidgetItem *tile = nullptr;
    for (int row = 0; row < s->tiles->count(); ++row)
        if (s->tiles->item(row)->data(Qt::UserRole).toString() == "R")
            tile = s->tiles->item(row);
    QPoint tilePoint = s->tiles->visualItemRect(tile).center();
    QPoint tileGlobal = s->tiles->viewport()->mapToGlobal(tilePoint);
    {
        QMouseEvent down(QEvent::MouseButtonPress, tilePoint, tileGlobal, Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(s->tiles->viewport(), &down);
        QMouseEvent up(QEvent::MouseButtonRelease, tilePoint, tileGlobal, Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(s->tiles->viewport(), &up);
    }
    check(s->doc.circuit.parts.size() == libraryParts, "tile click enters placement without adding a part");
    click(s->circuit, {120, 450});
    click(s->circuit, {150, 520});
    check(s->doc.circuit.parts.size() == libraryParts + 2, "tile click retains repeat placement");
    s->doc.undo.undo();
    s->doc.undo.undo();
    QMetaObject::invokeMethod(s->tiles, "componentDragStarted", Qt::DirectConnection);
    QScopedPointer<QMimeData> partMime(s->tiles->mimeDataForKind("R"));
    QPoint dropPoint = s->circuit->viewport()->rect().center() + QPoint(100, 70);
    QDragEnterEvent componentEnter(dropPoint, Qt::CopyAction, partMime.data(), Qt::LeftButton,
                                   Qt::NoModifier);
    QApplication::sendEvent(s->circuit->viewport(), &componentEnter);
    QDropEvent componentDrop(dropPoint, Qt::CopyAction, partMime.data(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s->circuit->viewport(), &componentDrop);
    check(componentDrop.isAccepted() && s->doc.circuit.parts.size() == libraryParts + 1 &&
              s->circuit->cursor().shape() == Qt::ArrowCursor,
          "component drop adds exactly one part in selection mode");
    click(s->circuit, s->circuit->mapToScene(dropPoint + QPoint(30, 30)));
    check(s->doc.circuit.parts.size() == libraryParts + 1, "click after drop does not repeat placement");
    s->doc.undo.undo();
    s->setTool("select");
    QPointF beforeClear = s->circuit->mapToScene(s->circuit->viewport()->rect().center());
    click(s->circuit, s->circuit->mapToScene(QPoint(12, 12)));
    check(s->circuit->scene()->selectedItems().isEmpty() && s->selected.isEmpty() &&
              QLineF(beforeClear, s->circuit->mapToScene(s->circuit->viewport()->rect().center())).length() <
                  1,
          "empty left-click clears selection without panning");
    click(s->circuit, s->doc.circuit.part("C1")->position);
    check(s->selected == "C1" && !s->circuit->scene()->selectedItems().isEmpty(),
          "left-click selects component");
    int parts = s->doc.circuit.parts.size();
    s->circuit->place("R");
    click(s->circuit, {120, 450});
    check(s->doc.circuit.parts.size() == parts + 1, "mouse placement");
    s->doc.undo.undo();
    check(s->doc.circuit.parts.size() == parts, "undo placement");
    int wires = s->doc.circuit.wires.size();
    s->setTool("wire");
    click(s->circuit, pinPosition(*s->doc.circuit.part("RA"), 0, true));
    click(s->circuit, pinPosition(*s->doc.circuit.part("CC"), 0, true));
    check(s->doc.circuit.wires.size() == wires + 1, "mouse pin wiring");
    s->doc.undo.undo();
    for (int rotation : {0, 90})
    {
        if (rotation)
            s->doc.edit("test rotation", [](Circuit &c) { c.part("U1")->rotation = 90; });
        bool correct = true;
        for (int pin = 0; pin < 8; pin++)
        {
            click(s->circuit, pinPosition(*s->doc.circuit.part("RA"), 0, true));
            click(s->circuit, pinPosition(*s->doc.circuit.part("U1"), pin, true));
            bool added = s->doc.circuit.wires.size() == wires + 1;
            correct &= added && s->doc.circuit.wires.back().b == Pin{"U1", pin};
            if (added)
                s->doc.undo.undo();
        }
        check(correct, rotation ? "rotated 555 pin targeting" : "all eight 555 pins target correctly");
        if (rotation)
            s->doc.undo.undo();
    }
    s->setTool("select");
    s->selected = "C1";
    s->refreshProperties();
    s->reference->setText("CAP_TEST");
    s->renamePart();
    check(s->doc.circuit.part("CAP_TEST") && s->doc.circuit.probes["timing"].part == "CAP_TEST",
          "rename linked pins");
    s->doc.undo.undo();
    s->selected = "C1";
    s->refreshProperties();
    s->value->setText("-1uF");
    s->applyProperty(false);
    s->toggleRun();
    check(s->propertyError && s->phase == "Ready", "invalid value blocks simulation");
    s->value->setText("10uF");
    s->applyProperty(false);
    check(!s->propertyError, "corrected value");
    s->mergeViews();
    QCoreApplication::processEvents();
    QScopedPointer<QMimeData> splitMime(s->left->mimeDataForTab(s->left->indexOf(s->schematic)));
    QPoint edgePoint(8, s->circuit->viewport()->height() / 2);
    QDragEnterEvent tabEnter(edgePoint, Qt::MoveAction, splitMime.data(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s->circuit->viewport(), &tabEnter);
    QDragMoveEvent tabMove(edgePoint, Qt::MoveAction, splitMime.data(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s->circuit->viewport(), &tabMove);
    auto *overlay = s->left->findChild<QFrame *>("workspaceDropOverlay");
    check(tabMove.isAccepted() && overlay && !overlay->isHidden() && overlay->width() < s->left->width() * .6,
          "tab drag over canvas shows half-pane split overlay");
    grab().save("work/dock-overlay-left.png");
    QDragLeaveEvent tabLeave;
    QApplication::sendEvent(s->circuit->viewport(), &tabLeave);
    check(overlay->isHidden(), "docking overlay clears on drag leave");
    QDragEnterEvent splitAgain(edgePoint, Qt::MoveAction, splitMime.data(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s->circuit->viewport(), &splitAgain);
    QDropEvent splitDrop(edgePoint, Qt::MoveAction, splitMime.data(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s->circuit->viewport(), &splitDrop);
    check(splitDrop.isAccepted() && s->left->count() == 1 && s->right->count() == 1 && overlay->isHidden(),
          "overlay split drop transfers tab and clears preview");
    check(s->left->count() == 1 && s->right->count() == 1, "split tabs");
    QCoreApplication::processEvents();
    pan(s->schematic, Qt::RightButton, false, s->schematic->viewport()->rect().center());
    QScopedPointer<QMimeData> mergeMime(s->right->mimeDataForTab(s->right->indexOf(s->schematic)));
    QPoint mergePoint = s->circuit->viewport()->rect().center();
    QDragEnterEvent mergeEnter(mergePoint, Qt::MoveAction, mergeMime.data(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s->circuit->viewport(), &mergeEnter);
    check(mergeEnter.isAccepted() && !overlay->isHidden() && overlay->width() > s->left->width() * .9,
          "center docking shows full-pane overlay");
    grab().save("work/dock-overlay-center.png");
    QDropEvent mergeDrop(mergePoint, Qt::MoveAction, mergeMime.data(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s->circuit->viewport(), &mergeDrop);
    check(mergeDrop.isAccepted() && s->left->count() == 2 && s->right->count() == 0 && s->right->isHidden() &&
              overlay->isHidden(),
          "center drop merges tabs and clears preview");
    s->mergeViews();
    s->doc.edit("test value", [](Circuit &c) { c.part("C1")->value = 20e-6; });
    s->doc.undo.undo();
    check(std::abs(s->doc.circuit.part("C1")->value - 1e-5) < 1e-14, "undo parameter");
    QString error;
    QTemporaryDir tmp(QDir::currentPath() + "/work/ui-test-XXXXXX");
    check(s->doc.save(tmp.filePath("smoke.cmp"), error), "save project");
    s->toggleRun();

    QTimer::singleShot(200, this,
                       [=]
                       {
                           s->run->click();
                           check(s->phase == "Paused", "combined button pause");
                           QTimer::singleShot(150, this,
                                              [=]
                                              {
                                                  s->run->click();
                                                  check(s->phase == "Running", "combined button resume");
                                              });
                       });
    QTimer::singleShot(550, this, [=] { s->stop->click(); });
    QTimer::singleShot(
        700, this,
        [=]
        {
            check(s->phase == "Stopped" && !s->plot->samples.isEmpty(), "stop retains samples");
            auto count = s->plot->samples.size();
            QTimer::singleShot(100, this,
                               [=]
                               {
                                   check(s->plot->samples.size() == count, "stop freezes samples");
                                   s->resetRun();
                                   check(s->plot->samples.isEmpty() && s->phase == "Ready",
                                         "reset clears samples");
                                   QTimer::singleShot(50, this, [=] { s->toggleRun(); });
                               });
        });
    QTimer::singleShot(
        9500, this,
        [=]
        {
            check(s->phase == "Complete" && !s->plot->samples.isEmpty(), "solver completion after restart");
            double parsed = 0;
            check(parseQuantity(s->value->text(), parsed), "formatted value parses");
            {
                QFile debug("work/property-debug.txt");
                debug.open(QIODevice::WriteOnly);
                QTextStream out(&debug);
                out << s->selected << "\n"
                    << s->value->text() << "\n"
                    << s->value->styleSheet() << "\npropertyError=" << s->propertyError << "\n";
            }
            QString err;
            check(s->plot->exportCsv("work/ui-waveforms.csv", err), "CSV export");
            const auto circuitBeforeTheme = s->doc.circuit.json();
            const auto sampleCount = s->plot->samples.size();
            const int undoBeforeTheme = s->doc.undo.index();
            const double oldStart = s->plot->start, oldEnd = s->plot->end, oldCursor = s->plot->cursor;
            const bool oldVisible = s->plot->traces.front().visible;
            s->plot->start = .5;
            s->plot->end = 2.5;
            s->plot->cursor = 1;
            s->plot->traces.front().visible = false;
            auto *darkAction = findChild<QAction *>("appearance_dark");
            auto *lightAction = findChild<QAction *>("appearance_light");
            darkAction->trigger();
            check(isDarkTheme() && darkAction->isChecked() &&
                      QSettings().value("appearance/mode").toString() == "dark",
                  "View Appearance Dark applies and persists its exclusive selection");
            QCoreApplication::processEvents();
            lightAction->trigger();
            check(!isDarkTheme() && lightAction->isChecked() && !darkAction->isChecked() &&
                      QSettings().value("appearance/mode").toString() == "light",
                  "View Appearance Light applies and persists its exclusive selection");
            check(s->doc.circuit.json() == circuitBeforeTheme && s->doc.undo.index() == undoBeforeTheme &&
                      s->plot->samples.size() == sampleCount && s->plot->start == .5 && s->plot->end == 2.5 &&
                      s->plot->cursor == 1 && !s->plot->traces.front().visible,
                  "appearance switching retains circuit undo results waveform zoom cursor and trace "
                  "visibility");
            s->plot->start = oldStart;
            s->plot->end = oldEnd;
            s->plot->cursor = oldCursor;
            s->plot->traces.front().visible = oldVisible;
            QCoreApplication::processEvents();
            grab().save("work/cmp-light.png");
            auto captureSchematicLabels = [&](const QString &path)
            {
                s->left->setCurrentWidget(s->schematic);
                QCoreApplication::processEvents();
                s->schematic->resetTransform();
                s->schematic->scale(1.5, 1.5);
                s->schematic->centerOn(s->doc.circuit.part("U1")->position);
                s->schematic->grab().save(path);
                s->schematic->fit();
                s->left->setCurrentWidget(s->circuit);
                QCoreApplication::processEvents();
            };
            auto checkGrip = [&]
            {
                auto *grip = findChild<QSizeGrip *>("windowResizeGrip");
                if (!grip)
                    return false;
                const auto image = grip->grab().toImage();
                int visiblePixels = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                        visiblePixels += image.pixelColor(x, y).rgb() != theme().panel.rgb();
                return visiblePixels > 10 && grip->width() >= 18 &&
                       (isMaximized() || isFullScreen() || grip->isVisible());
            };
            check(checkGrip(), "status resize grip has a visible high-contrast drawing in light mode");
            captureSchematicLabels("work/schematic-labels-light.png");
            darkAction->trigger();
            QCoreApplication::processEvents();
            grab().save("work/cmp-dark.png");
            check(checkGrip(), "status resize grip has a visible high-contrast drawing in dark mode");
            captureSchematicLabels("work/schematic-labels-dark.png");
            showDocumentation("README.md");
            QCoreApplication::processEvents();
            if (auto *viewer = findChild<QDialog *>("documentationViewer"))
            {
                viewer->grab().save("work/cmp-dark-documentation.png");
                viewer->close();
            }
            findChild<QAction *>("appearance_system")->trigger();
            check(isDarkTheme() == (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark),
                  "Follow System resolves the current OS color scheme");
            setAppearance(originalAppearance);
            if (savedAppearance.isValid())
                QSettings().setValue("appearance/mode", savedAppearance);
            else
                QSettings().remove("appearance/mode");
            QCoreApplication::processEvents();
            grab().save("work/cmp-ui.png");
            s->left->setCurrentWidget(s->schematic);
            QCoreApplication::processEvents();
            grab().save("work/cmp-schematic.png");
            s->splitViews();
            grab().save("work/cmp-split.png");
            QFile f("work/ui-smoke.txt");
            f.open(QIODevice::WriteOnly);
            f.write(*result ? "PASS: placement, pin wiring, rename, invalid-value rejection, "
                              "undo, save, native solver, combined "
                              "Run/Pause, Stop/Reset, CSV, tab split/merge\n"
                            : "FAIL\n");
            s->circuit->selectPart("C1");
            s->schematic->selectPart("C1");
            s->circuit->grab().save("work/component-selection.png");
            QCoreApplication::exit(*result ? 0 : 2);
        });
}
} // namespace cmp
