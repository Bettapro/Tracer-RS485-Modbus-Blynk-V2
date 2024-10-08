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

#pragma once

#include "../incl/include_all_core.h"

#if defined(USE_MQTT) && !defined(USE_MQTT_HOME_ASSISTANT)

#include "../core/BaseSync.h"
#include "../core/Controller.h"
#include "../core/VariableDefiner.h"
#include "../incl/include_all_lib.h"

class MqttSync : public BaseSync {
    public:
        static MqttSync &getInstance() {
            static MqttSync instance;
            return instance;
        }

        void setup();
        void connect(bool blocking = true);
        void loop();
        inline bool isVariableAllowed(const VariableDefinition *def);
        bool sendUpdateToVariable(const VariableDefinition *def, const void *value);
        // upload values stats
        void uploadStatsToMqtt();
        // upload values realtime
        void uploadRealtimeToMqtt();

    private:
        MqttSync();

        PubSubClient *mqttClient;

        char mqttPublishBuffer[20];

#if defined(USE_MQTT_RPC_SUBSCRIBE) || defined(USE_MQTT_JSON_PUBLISH)
        DynamicJsonDocument syncJson(1024);
#endif

        bool initialized;
};

#endif
