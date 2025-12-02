#ifndef SPEAKER_H
#define SPEAKER_H

void speaker_init();
void speaker_set_alarm(bool on);   // continuous tone on/off
void speaker_beep_once(int ms);    // short beep (non-blocking)
void speaker_update();

#endif
