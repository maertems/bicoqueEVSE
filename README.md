# bicoqueEVSE
Control for simpleEVSE localy with screen and button and remotly over Wifi

## Parts Needed
- ESP8266 (The one used is a NodeMcu)
- Push button 
- LCD screen 20x4 I2C
- Relay (Optional)
- simpleEVSE ( http://evracing.cz/simple-evse-wallbox )

## Local actions
- Start / Stop charging
- View status of EVSE
- Simple settings like autostart / power / wifi enable

## Wifi API
- Get status
- Enable / Disable EVSE
- Set power. Can be done during charging
- View consumption for the current charge
- View consumption from the begining
