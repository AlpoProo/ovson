#include "Player.h"

CPlayer::CPlayer(jobject instance) {
	this->playerInstance = instance;
}

void CPlayer::Cleanup() {
	lc->env->DeleteLocalRef(this->playerInstance);
}