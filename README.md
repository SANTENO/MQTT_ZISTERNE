# MQTT_ZISTERNE
ESP32 controller to measure water level in outdoor tank with minimal integration to home control with MQTT and IOBROKER
The project is based on idea and code from CT Make 6/2021 and is extended to:
- integrate into smart home control via MQTT, tested with IOBROKER
  - start the measurement via MQTT callback command   
- preset of 4 IP addresses for MQTT servers to allow exchange of home control devices and fallback solutions
   - dynamic fallback for not reachable MQTT connection   
- relays instead of Motor shield

MQTT: 
- submits:
  - water level height in cm
  - periodically sends athmoshperic pressure (check for reliability)
  - confirmation of measurement    
