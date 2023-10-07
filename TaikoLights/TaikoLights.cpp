#include <iostream>
#include <iCUESDK/iCUESDK.h>
#include <map>
#include <SDL.h>
#include <tuple>
#include <vector>


const char* corsairErrorToString(CorsairError error) {
	switch (error) {
	case CE_Success:
		return "CE_Success";
	case CE_NotConnected:
		return "CE_NotConnected";
	case CE_NoControl:
		return "CE_NoControl";
	case CE_IncompatibleProtocol:
		return "CE_IncompatibleProtocol";
	case CE_InvalidArguments:
		return "CE_InvalidArguments";
	default:
		return "Unknown error";
	}
}

void onCorsairSessionStateChange(void *context, const CorsairSessionStateChanged *eventData) {
	switch (eventData->state) {
		case CSS_Invalid:
			std::cout << "[iCUE] Session State Change returned invalid\n";
			return;
		case CSS_Closed:
			std::cout << "[iCUE] Connection closed\n";
			return;
		case CSS_Connecting:
			std::cout << "[iCUE] Connecting to iCUE\n";
			return;
		case CSS_Connected:
			std::cout << "[iCUE] Connected to Corsair iCUE. Server version v" << eventData->details.serverVersion.major << "." << eventData->details.serverVersion.minor << "." << eventData->details.serverVersion.patch
				<< " | Client version v" << eventData->details.clientVersion.major << "." << eventData->details.clientVersion.minor << "." << eventData->details.clientVersion.patch << std::endl;
			return;
		case CSS_ConnectionLost:
			std::cout << "[iCUE] Lost connection to iCUE\n";
			return;
		case CSS_Timeout:
			std::cout << "[iCUE] Timeout when connecting to iCUE; iCUE might not be installed on this computer\n";
			return;
		case CSS_ConnectionRefused:
			std::cout << "[iCUE] Connection refused; third-party control is disabled in iCUE settings";
			return;
	}
}

void onLEDsSet(void* context, CorsairError error) {}

std::map<std::string, std::pair<std::vector<CorsairLedLuid>, int>> getAllLeds(CorsairDeviceInfo* devices, int amountOfDevices) {
	std::map<std::string, std::pair<std::vector<CorsairLedLuid>, int>>ledPos = {};
	for (int i = 0; i < amountOfDevices; i++) {
		CorsairLedPosition ledPositions[CORSAIR_DEVICE_LEDCOUNT_MAX]{};
		std::vector<CorsairLedLuid> ledIds;
		int ledsOnboard = 0;

		CorsairError pos = CorsairGetLedPositions(devices[i].id, CORSAIR_DEVICE_LEDCOUNT_MAX, ledPositions, &ledsOnboard);

		if (pos != CE_Success) std::cerr << "[iCUE] Failed to get LED positions for device " << devices[i].id << "; SDK threw " << corsairErrorToString(pos) << std::endl;;

		for (int j = 0; j < ledsOnboard; j++) ledIds.push_back(ledPositions[j].id);
		ledPos[devices[i].id] = std::pair<std::vector<CorsairLedLuid>, int>(ledIds, ledsOnboard);
	}
	return ledPos;
}

void setAllLeds(std::map<std::string, std::pair<std::vector<CorsairLedLuid>, int>> positions, std::tuple<unsigned char, unsigned char, unsigned char> rgb, CorsairDeviceInfo* devices, int amountOfDevices) {
	for (int i = 0; i < amountOfDevices; i++) {
		std::pair<std::vector<CorsairLedLuid>, int> deviceLeds = positions[devices[i].id];
		CorsairLedColor newClrs[CORSAIR_DEVICE_LEDCOUNT_MAX]{};

		for (int j = 0; j < deviceLeds.second; j++) newClrs[j] = CorsairLedColor{ deviceLeds.first.at(j), std::get<0>(rgb), std::get<1>(rgb), std::get<2>(rgb), 255};

		CorsairError setLed = CorsairSetLedColorsBuffer(devices[i].id, deviceLeds.second, newClrs);

		if (setLed != CE_Success) std::cerr << "[iCUE] Failed to set LED positions for device " << devices[i].id << "; SDK threw " << corsairErrorToString(setLed) << std::endl;
	}
	CorsairAsyncCallback bHandler{ onLEDsSet };
	CorsairSetLedColorsFlushBufferAsync(bHandler, NULL);
}

void initSDL(std::map<std::string, std::pair<std::vector<CorsairLedLuid>, int>> ledPositions, CorsairDeviceInfo* devices, int amountOfDevices) {
	if (SDL_Init(SDL_INIT_JOYSTICK) < 0) {
		std::cerr << "[SDL2] Failed to initialise SDL: " << SDL_GetError() << std::endl;
		exit(1);
	}

	std::cout << "[SDL2] Initialised\n";
	std::cout << "[SDL2] Waiting for joystick\n";

	// Keep looping until a compatible controller is detected
	SDL_Joystick* joystick = SDL_JoystickOpen(0);
	while (SDL_NumJoysticks() < 1) joystick = SDL_JoystickOpen(0);

	if (!joystick) {
		std::cerr << "[SDL2] Failed to open controller: " << SDL_GetError() << std::endl;
		exit(1);
	}

	std::cout <<  "[SDL2] Using detected joystick \"" << SDL_JoystickName(joystick) << "\"\n";

	bool quit = false;

	// Controller buttons we care about
	const SDL_GameControllerButton redButtons[] = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
	const SDL_GameControllerButton blueButtons[] = { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER };

	// RGB tuples of desired colours
	const std::tuple<int, int, int> clrRed = std::make_tuple(255, 20, 20);
	const std::tuple<int, int, int> clrSRed = std::make_tuple(255, 0, 0);
	const std::tuple<int, int, int> clrBlue = std::make_tuple(104, 192, 192);
	const std::tuple<int, int, int> clrSBlue = std::make_tuple(0, 0, 255);
	const std::tuple<int, int, int> clrNone = std::make_tuple(0, 0, 0);

	// When two buttons are pressed, LEDs are set to a more intense shade of red/blue 
	bool firstKeyTriggered = false;

	while (!quit) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				quit = true;
				break;
			case SDL_JOYBUTTONDOWN:
				if (std::find(std::begin(redButtons), std::end(redButtons), event.jbutton.button) != std::end(redButtons)) {
					if (firstKeyTriggered) setAllLeds(ledPositions, clrSRed, devices, amountOfDevices);
					else setAllLeds(ledPositions, clrRed, devices, amountOfDevices);
				}
				else if (std::find(std::begin(blueButtons), std::end(blueButtons), event.jbutton.button) != std::end(blueButtons)) {
					if (firstKeyTriggered) setAllLeds(ledPositions, clrSBlue, devices, amountOfDevices);
					else setAllLeds(ledPositions, clrBlue, devices, amountOfDevices);
				}
				firstKeyTriggered = true;
				break;
			case SDL_JOYBUTTONUP:
				if ((std::find(std::begin(redButtons), std::end(redButtons), event.jbutton.button) != std::end(redButtons)) || (std::find(std::begin(blueButtons), std::end(blueButtons), event.jbutton.button) != std::end(blueButtons))) {
					setAllLeds(ledPositions, clrNone, devices, amountOfDevices);
					firstKeyTriggered = false;
					break;
				}
				break;
			case SDL_JOYAXISMOTION:
				// ZL = 4 / ZR = 5; SDL_JOYAXISMOTION also triggers for the controller gyro
				if (int axis = event.jaxis.axis; axis != 4 && axis != 5) break;

				if (event.jaxis.value > 0) {
					if (firstKeyTriggered) setAllLeds(ledPositions, clrSBlue, devices, amountOfDevices);
					else setAllLeds(ledPositions, clrBlue, devices, amountOfDevices);

					firstKeyTriggered = true;
				} else {
					setAllLeds(ledPositions, clrNone, devices, amountOfDevices);
					firstKeyTriggered = false;
				}
				break;
			}
		}
	}

	SDL_JoystickClose(joystick);
	SDL_Quit();
}

int main(int argc, char** args) {
	std::cout << "[INFO] Taiko Lights - Corsair iCUE Lighting for Taiko no Tatsujin Controller Mapping\n";

	CorsairSessionStateChangedHandler sscHandler{ onCorsairSessionStateChange };
	CorsairError conn = CorsairConnect(sscHandler, NULL);

	int devicesFound = 0;
	CorsairDeviceInfo devices[10]{};
	CorsairDeviceFilter ft{ CDT_All };

	if (conn == CE_Success) {
		CorsairError getDevices = CorsairGetDevices(&ft, CORSAIR_DEVICE_COUNT_MAX, devices, &devicesFound);
		// The SDK does not connect instantly, and will throw CE_NotConnected a lot
		while (getDevices == CE_NotConnected) getDevices = CorsairGetDevices(&ft, CORSAIR_DEVICE_COUNT_MAX, devices, &devicesFound);
		if (getDevices != CE_Success) std::cerr << "[iCUE] Failed to get devices; threw Corsair error " << corsairErrorToString(getDevices) << std::endl;
	}

	if (devicesFound > 0) {
		std::cout << "[iCUE] Found the following Corsair iCUE devices:\n";
		for (int i = 0; i < devicesFound; i++) std::cout << "[iCUE] " << devices[i].model << " " << devices[i].id << std::endl;
	}

	std::map<std::string, std::pair<std::vector<CorsairLedLuid>, int>> ledPos = getAllLeds(devices, devicesFound);
	initSDL(ledPos, devices, devicesFound);

	return 0;
}