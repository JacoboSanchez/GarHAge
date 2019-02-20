#include "Arduino.h"
#include "Door.h"

Door::Door(char* alias, char* action_topic, char* status_topic)
{
  _alias = alias;
  _mqtt_action_topic = action_topic;
  _mqtt_status_topic = status_topic;
  _statusEnabled = false;
}

Door::Door(char* alias, char* action_topic, char* status_topic, int statusPin, char* statusLogic)
{
  _alias = alias;
  _mqtt_action_topic = action_topic;
  _mqtt_status_topic = status_topic;
  _statusEnabled = true;
  _statusPin = statusPin;
  _statusSwitchLogic = statusLogic;

  _lastStatusValue = -1;
  _lastSwitchTime = 0;
  _statusString = "";
}

bool Door::isOpen() {
  if ( (strcmp(_statusSwitchLogic, "NO") && digitalRead(_statusPin) == HIGH) 
    || (strcmp(_statusSwitchLogic, "NC") || digitalRead(_statusPin) == LOW) ) {
    return true;
  }
  else {
    return false;
  }
}

GarageDoor::GarageDoor(char* alias, char* action_topic, char* status_topic, int openPin, int closePin)
  : Door(alias, action_topic, status_topic)
{
  _openPin = openPin;
  _closePin = closePin;
}

GarageDoor::GarageDoor(char* alias, char* action_topic, char* status_topic, int openPin, int closePin, int statusPin, char* logic)
  : Door(alias, action_topic, status_topic, statusPin, logic)
{
  _openPin = openPin;
  _closePin = closePin;
}