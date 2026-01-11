#pragma once
#include "Player.h"

class CMinecraft {
public:
	jclass GetClass();
	jobject GetInstance();

	CPlayer GetLocalPlayer();
};