/**
 * Solar Tracer Blynk V3 [https://github.com/Bettapro/Solar-Tracer-Blynk-V3]
 * Copyright (c) 2021 Alberto Bettin
 *
 * Based on the work of @jaminNZx and @tekk.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "MqttSync.h"

#include "../incl/include_all_core.h"

#if defined(USE_MQTT) && !defined(USE_MQTT_HOME_ASSISTANT)

#include "../core/datetime.h"

#define RETAIN_ALL_MSG false
#define MQTT_CONNECT_ATTEMPT 3

#if defined(USE_MQTT_RPC_SUBSCRIBE)
DynamicJsonDocument json(1024);
#endif
void mqttCallback(char *topic, uint8_t *bytes, unsigned int length) {
    String payload;
    for (int i = 0; i < length; i++) {
        payload += (char)bytes[i];
    }

#ifdef USE_MQTT_RPC_SUBSCRIBE
    deserializeJson(json, payload);
    const char *constTopic = json["method"].as<const char *>();
    payload = json["params"].as<String>();
#else
    const char *constTopic = topic;
#endif

    const VariableDefinition *def = VariableDefiner::getInstance().getDefinitionByMqttTopic(topic);

    if (def != nullptr && def->mode == MD_READWRITE) {
        if (def->source == VariableSource::SR_INTERNAL) {
            switch (def->variable) {
                case Variable::REALTIME_CLOCK: {
                    if (payload.toInt() > 0) {
                        debugPrintln("UPDATE CONTROLLER DATETIME");
                        if (Datetime::getMyNowTm() != nullptr) {
                            Controller::getInstance().getSolarController()->syncRealtimeClock(Datetime::getMyNowTm());
                        }
                    }
                } break;
                case Variable::UPDATE_ALL_CONTROLLER_DATA: {
                    if (payload.toInt() > 0) {
                        debugPrintln("REQUEST ALL VALUES TO CONTROLLER");
                        Controller::getInstance().getSolarController()->fetchAllValues();
                        MqttSync::getInstance().uploadRealtimeToMqtt();
                        MqttSync::getInstance().uploadStatsToMqtt();
                    }
                } break;
            }
        } else {
            switch (def->datatype) {
                case VariableDatatype::DT_UINT16: {
                    uint16_t newState = payload.toInt();
                    MqttSync::getInstance().applyUpdateToVariable(def->variable, &newState, false);
                }
                case VariableDatatype::DT_FLOAT: {
                    float newState = payload.toFloat();
                    MqttSync::getInstance().applyUpdateToVariable(def->variable, &newState, false);
                }
                case VariableDatatype::DT_BOOL: {
                    bool newState = payload.toInt() > 0;
                    MqttSync::getInstance().applyUpdateToVariable(def->variable, &newState, false);
                } break;
            }
        }
    }
}

MqttSync::MqttSync() {
    WiFiClient *espClient = new WiFiClient();
    this->mqttClient = new PubSubClient(*espClient);
}

void MqttSync::setup() {
    this->mqttClient->setServer(Environment::getData()->mqttServerHostname, Environment::getData()->mqttServerPort);

    mqttClient->setCallback(mqttCallback);
    for (uint8_t index = 0; index < Variable::VARIABLES_COUNT; index++) {
        const VariableDefinition *def = VariableDefiner::getInstance().getDefinition((Variable)index);
        if (def->mqttTopic != nullptr && def->mode == MD_READWRITE && (def->source == VariableSource::SR_INTERNAL || Controller::getInstance().getSolarController()->isVariableEnabled(def->variable))) {
            this->mqttClient->subscribe(def->mqttTopic);
        }
    }

    this->connect();
}

void MqttSync::connect(bool blocking) {
    debugPrintf(true, Text::setupWithName, "MQTT");
    debugPrint(Text::connecting);

    uint8_t counter = 0;

    do {
        mqttClient->connect(

            Environment::getData()->mqttClientId,
            strlen(Environment::getData()->mqttUsername) > 0 ? Environment::getData()->mqttUsername : nullptr,
            strlen(Environment::getData()->mqttPassword) > 0 ? Environment::getData()->mqttPassword : nullptr);
        while (!mqttClient->connected() && counter < 10) {
            debugPrint(Text::dot);
            delay(500);
            counter++;
        }

        if (mqttClient->state() != MQTT_CONNECTED) {
            debugPrintf(true, Text::errorWithCode, mqttClient->state());
        } else {
            debugPrintln(Text::ok);
        }

    } while (blocking && !mqttClient->connected());

    Controller::getInstance().setErrorFlag(STATUS_ERR_NO_MQTT_CONNECTION, !mqttClient->connected());
}
void MqttSync::loop() {
    Controller::getInstance().setErrorFlag(STATUS_ERR_NO_MQTT_CONNECTION, !mqttClient->connected());
    mqttClient->loop();
}
bool MqttSync::isVariableAllowed(const VariableDefinition *def) {
    return def->mqttTopic != nullptr;
}
bool MqttSync::sendUpdateToVariable(const VariableDefinition *def, const void *value) {
    switch (def->datatype) {
        case VariableDatatype::DT_UINT16:
#ifdef USE_MQTT_JSON_PUBLISH
            syncJson[def->mqttTopic] = *(uint16_t *)value;
            return true;
#else
            dtostrf(*(uint16_t *)value, 0, 0, mqttPublishBuffer);
#endif
        case VariableDatatype::DT_FLOAT:
#ifdef USE_MQTT_JSON_PUBLISH
            syncJson[def->mqttTopic] = *(float *)value;
            return true;
#else
            dtostrf(*(float *)value, 0, 4, mqttPublishBuffer);
            return mqttClient->publish(def->mqttTopic, mqttPublishBuffer, RETAIN_ALL_MSG);
#endif
        case VariableDatatype::DT_BOOL:
#ifdef USE_MQTT_JSON_PUBLISH
            syncJson[def->mqttTopic] = *(bool *)value;
            return true;
#else
            return mqttClient->publish(def->mqttTopic, (*(const bool *)value) ? "1" : "0", RETAIN_ALL_MSG);
#endif
        case VariableDatatype::DT_STRING:
#ifdef USE_MQTT_JSON_PUBLISH
            syncJson[def->mqttTopic] = *(const char *)value;
            return true;
#else
            return mqttClient->publish(def->mqttTopic, (const char *)value, RETAIN_ALL_MSG);
#endif
    }
    return false;
}

// upload values stats
void MqttSync::uploadStatsToMqtt() {
    if (!this->mqttClient->connected()) {
        return;
    }

    MqttSync::getInstance().sendUpdateAllBySource(VariableSource::SR_STATS, false);
#ifdef USE_MQTT_JSON_PUBLISH
    String output;
    serializeJson(syncJson, output);
    if (syncJson.size() > 0) {
        mqttClient->publish(MQTT_JSON_PUBLISH_TOPIC, output.c_str());
    }
#endif
}

// upload values realtime
void MqttSync::uploadRealtimeToMqtt() {
    if (!this->mqttClient->connected()) {
        return;
    }

#ifdef MQTT_TOPIC_INTERNAL_STATUS
    float status = Controller::getInstance().getStatus();
    this->sendUpdateToVariable(VariableDefiner::getInstance().getDefinition(Variable::INTERNAL_STATUS), &(status));
#endif
    this->sendUpdateAllBySource(VariableSource::SR_REALTIME, false);
#ifdef USE_MQTT_JSON_PUBLISH
    String output;
    serializeJson(syncJson, output);
    if (syncJson.size() > 0) {
        mqttClient->publish(MQTT_JSON_PUBLISH_TOPIC, output.c_str());
    }
#endif
}

#endif
