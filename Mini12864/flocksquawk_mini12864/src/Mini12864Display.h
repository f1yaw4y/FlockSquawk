#ifndef MINI12864_DISPLAY_H
#define MINI12864_DISPLAY_H

void Mini12864DisplayBegin();
void Mini12864DisplayUpdate();
void Mini12864DisplayNotifySystemReady();
void Mini12864DisplayShowAlert();
bool Mini12864DisplayConsumeVolume(float* volumeOut);
bool Mini12864DisplayConsumeAlertTest();
void Mini12864DisplayNotifyWifiFrame(const uint8_t mac[6], uint8_t channel, int8_t rssi);
bool Mini12864DisplayIsActive();

#endif
