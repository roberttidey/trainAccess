# UK train Display
Displays current departures from a particular UK rail station

Construction details will appear at instructables shortly

## Features
- Accesses National Rail station database to get departure times for a specified station
- Can filter list to show trains going to specific destination
- ESP8266 based processing, hooks onto local wifi network
- Battery powered (rechargeable LIPO) with inbuilt charger
- Very low quiescent current (< 20uA) for long battery life
- 320 x 240 LCD display with 3 control buttons
- Normally the module sleeps after a time but can be turned into a non sleep mode for checking and configuration
- Configuration data in a file to allow set up of major parameters
- Web based file access to update config file
- Software can be updated via web interface
- Low cost
- 3D printed enclosure

## Library details
- Uses standard libraries plus TFT_eSPI for fast display control
- TFT_eSPI can be loaded from Arduino library manager

## Set up
- Modify ssid and 3 password fields to suit personal needs
- Compile with Arduino IDE
- Serial upload
- Use the WifiManager AP to select and set up wifi connection
- Register with National Rail and get access token for OpenLDBWS (see link below)
- Modify trainsConfig.txt
	- Change Access token
	- Change code for local station (see link below)
	- Change code for destination or leave blank for all destinations
	- Change refresh and sleep settings as required
- Ipload the files in data folder using http:ip/upload
- Further file access can use http:ip/edit
- For software updating export a binary and then access http:/ip/firmware

## Usage
- Middle button is used to turn unit on. It will automatically get current times and refresh at the specified interval
- It will sleep automatically after the configured time if enabled
- Sleep may be manually forced by a long press of the middle button
- If there are more listings than the display size then the top and bottom buttons will page through the screens.

## Links
Instructables TBA

Thingiverse Enclosure TBA

National Rail registration https://www.nationalrail.co.uk/100296.aspx

Station codes https://www.nationalrail.co.uk/stations_destinations/48541.aspx


