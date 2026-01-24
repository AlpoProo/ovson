#include "Player.h"

CPlayer::CPlayer(jobject instance) {
	this->playerInstance = instance;
}

void CPlayer::Cleanup() {
	JNIEnv* env = lc->getEnv();
	if (env) env->DeleteLocalRef(this->playerInstance);
}