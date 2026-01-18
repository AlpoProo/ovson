#pragma once
#include "../Java.h"

class CPlayer {
public:
	CPlayer(jobject instance);
	void Cleanup();
    jobject Get() const { return playerInstance; }
private:
	jobject playerInstance;
};