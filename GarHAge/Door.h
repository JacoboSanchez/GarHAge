#ifndef Door_h
#define Door_h

#include "Arduino.h"

class Door
{
  public:
    Door(char*, char*, char*);
    Door(char*, char*, char*, int, char*);
    // Getters and Setters
    char* getAlias() { return _alias; };
    char* getMqttAction() { return _mqtt_action_topic; };
    char* getMqttStatus() { return _mqtt_status_topic; };
    bool statusEnabled() { return _statusEnabled; };
    int getStatusPin() { return _statusPin; };
    char* getStatusSwitchLogic() { return _statusSwitchLogic; };
    int getLastStatusValue() { return _lastStatusValue; };
    void setLastStatusValue(int statusValue) { _lastStatusValue = statusValue; };
    unsigned long getLastSwitchTime() { return _lastSwitchTime; };
    void setLastSwitchTime(unsigned long switchTime) { _lastSwitchTime = switchTime; };
    String getStatusString() { return _statusString; };
    void setStatusString(String stat) { _statusString = stat; };

    bool isOpen();
    virtual bool hasControl() { return false; };
    virtual int getOpenPin() { return -1; };
    virtual int getClosePin() { return -1; };
    
  private:
    char* _alias;
    char* _mqtt_action_topic;
    char* _mqtt_status_topic;
    bool _statusEnabled;
    int _statusPin;
    char* _statusSwitchLogic;

    int _lastStatusValue;
    unsigned long _lastSwitchTime;
    String _statusString;
};

class GarageDoor : public Door {
  public:
    GarageDoor(char*, char*, char*, int, int);
    GarageDoor(char*, char*, char*, int, int, int, char*);
    // Getters and Setters
    int getOpenPin() { return _openPin; };
    int getClosePin() { return _closePin; };

    bool hasControl() { return true; };
  private:
    int _openPin;
    int _closePin;
};

#endif
