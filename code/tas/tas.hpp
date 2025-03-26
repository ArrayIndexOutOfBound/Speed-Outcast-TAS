#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>


class TAS {
public:
	TAS();
	~TAS();
	void start();
	void stop();
	void notifyNewFrame();
	void makeOperationThisFrame();
	void readTasFile();
	void sendInputsThisFrame();

	void proofOfConcept();

private:
	void infiniteLoop();
	bool gamestateChanged = false;
	bool firstIteration = true;

	std::atomic<bool> terminateThread = false;
	std::thread workerThread;
	int32_t starterFrame = 0;
	int32_t currentFrame = 0;
	int32_t sleepFrame = 0;
	int32_t sleepDuration = 0;
	char lastKnownMap [64] = "";
	std::vector<std::string> allActionBuffer;

	// "Actions"
	int8_t ActionsBuffer = 0x00000000;
	int8_t ACT_FORWARD =	0x00000001;
	int8_t ACT_BACKWARD =	0x00000010;
	int8_t ACT_LEFT =		0x00000100;
	int8_t ACT_RIGHT =		0x00001000;
	int8_t ACT_JUMP =		0x00010000;
	int8_t ACT_CROUCH =		0x00100000;
	int8_t ACT_USE =		0x01000000;
	int8_t ACT_NOTUSED =	0x10000000;
	// Mouse
	int8_t MouseBuffer = 0x00000000;
	int8_t LCLICK = 0x00000001;
	int8_t RCLICK = 0x00000010;
	// Inventory
	int8_t InventoryBuffer = 0x00000000;
	int8_t INV_BACTA =	0x00000001;
	int8_t INV_SEEKER = 0x00000010;
	// Powers
	int8_t PowersBuffer = 0x00000000;
	int8_t FP_PUSH =	0x00000001;
	int8_t FP_PULL =	0x00000010;
	int8_t FP_SPEED =	0x00000100;
	int8_t FP_MIND =	0x00001000;
	int8_t FP_HEAL =	0x00010000;
	int8_t FP_SABER =	0x00100000; // Saber style
	// Game manipulation
	int8_t GameMBuffer = 0x00000000;
	int8_t QUICKS = 0x00000001;
	int8_t QUICKL = 0x00000010;
	int8_t HARDS =	0x00000100;
	int8_t HARDL =	0x00001000;
	int8_t VIDR =	0x00010000;
	int8_t SNDR =	0x00100000;
	// Weapons
	int16_t WeaponBuffer = 0x0000000000000000; // Maybe for all weapons
	int8_t WP_SABER =	0x00000001;
	int8_t WP_E11 =		0x00000010;
	int8_t WP_BOW =		0x00000100;
	int8_t WP_REP =		0x00001000;
	int8_t WP_FLECH =	0x00010000;
	int8_t WP_TD =		0x00100000;
	int8_t WP_MINES =	0x01000000;
	int8_t WP_BOMB =	0x10000000;

};
