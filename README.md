# MQTT_ZISTERNE
ESP32 controller to measure water level in outdoor tank with minimal integration to home control with MQTT and IOBROKER
The project is based on idea and code from CT Make 6/2021 and is extended to:
- integrat into smart home control via MQTT, tested with IOBROKER
- preset of upto 4 IP address for MQTT servers to allow exchange of home control devices and fallback solutions
   - all presets are programmable with serial port
- relays instead of Motor shield

Further ideas: Integrate water pressure sensor for check of waterpump and valve control of the tank - 
