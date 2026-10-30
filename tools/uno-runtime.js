function createUno(bytes){
 var flash=new Uint8Array(32768);flash.fill(255);flash.set(bytes);
 var cpu=new AVR.CPU(new Uint16Array(flash.buffer),2048);
 var ports=[new AVR.AVRIOPort(cpu,AVR.portDConfig),new AVR.AVRIOPort(cpu,AVR.portBConfig),new AVR.AVRIOPort(cpu,AVR.portCConfig)];
 var timers=[new AVR.AVRTimer(cpu,AVR.timer0Config),new AVR.AVRTimer(cpu,AVR.timer1Config),new AVR.AVRTimer(cpu,AVR.timer2Config)];
 var adc=new AVR.AVRADC(cpu,AVR.adcConfig);var uart=new AVR.AVRUSART(cpu,AVR.usart0Config,16000000);
 var reached=0,executed=0;
 var serial='';uart.onByteTransmit=function(b){if(serial.length<4096)serial+=String.fromCharCode(b);};
 function pin(i){return i<8?[0,i]:i<14?[1,i-8]:[2,i-14];}
 return {advance:function(cycles,inputs,supply,reference,budget){
  adc.avcc=supply;adc.aref=reference;
  for(var i=0;i<20;i++){var pos=pin(i);ports[pos[0]].setPin(pos[1],inputs[i]>.6*supply);if(i>=14)adc.channelValues[i-14]=Math.max(0,Math.min(supply,inputs[i]));}
  executed=0;while(cpu.cycles<cycles && executed<budget){AVR.avrInstruction(cpu);cpu.tick();executed++;}reached=cpu.cycles;
  var result=[];for(var i=0;i<20;i++){var pos=pin(i);result.push(ports[pos[0]].pinState(pos[1]));}return result;
 },report:function(){return [reached,executed];},read:function(address){return cpu.readData(address);},cycles:function(){return cpu.cycles;},serial:function(){return serial;}};
}
