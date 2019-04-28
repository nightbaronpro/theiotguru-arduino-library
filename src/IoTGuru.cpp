/**
 * IoTGuru.h - Arduino client of the https://iotguru.live cloud services.
 */
#include "IoTGuru.h"

IoTGuru::IoTGuru(String userShortId, String deviceShortId, String deviceKey) {
    this->userShortId = userShortId;
    this->deviceShortId = deviceShortId;
    this->deviceKey = deviceKey;

    this->callback = NULL;
}


IoTGuru& IoTGuru::setCallback(IOT_GURU_CALLBACK_SIGNATURE) {
    this->callback = callback;

    return *this;
}

IoTGuru* IoTGuru::setCheckDuration(unsigned long checkDuration) {
    this->checkDuration = checkDuration;

    return this;
}


IoTGuru* IoTGuru::setDebugPrinter(HardwareSerial* debugPrinter) {
    this->debugPrinter = debugPrinter;

    return this;
}

IoTGuru* IoTGuru::setNetworkClient(Client* client) {
    this->networkClient = client;
    this->mqttClient = MqttClient(*client);

    return this;
}

boolean IoTGuru::check() {
    if (lastChecked == 0 || lastChecked + checkDuration < millis()) {
        lastChecked = millis();
    } else {
        return false;
    }

    IOTGURU_DEBUG_PRINT("ENTRY");

    IOTGURU_DEBUG_PRINT("Send request to the cloud");
    HTTPClient httpClient;
    httpClient.useHTTP10(true);
    httpClient.setTimeout(1000);

    httpClient.begin(String(IOT_GURU_BASE_URL) + "firmware/check/" + this->deviceKey);
    int code = httpClient.GET();
    httpClient.end();

    IOTGURU_DEBUG_PRINT("Response received from the cloud (status code " + String(code) + ")");

    IOTGURU_DEBUG_PRINT("EXIT");
    return code == 200;
}

boolean IoTGuru::check(const char* ota_version) {
    if (lastChecked == 0 || lastChecked + checkDuration < millis()) {
        lastChecked = millis();
    } else {
        return false;
    }

    IOTGURU_DEBUG_PRINT("ENTRY");

    String url = String(IOT_GURU_BASE_URL) + "firmware/check/" + this->deviceKey + "/" + ota_version;
    IOTGURU_DEBUG_PRINT("Send request to the cloud: " + url);
    HTTPClient httpClient;
    httpClient.useHTTP10(true);
    httpClient.setTimeout(1000);

    httpClient.begin(url);
    int code = httpClient.GET();
    httpClient.end();

    IOTGURU_DEBUG_PRINT("Response received from the cloud (status code " + String(code) + ")");

    IOTGURU_DEBUG_PRINT("EXIT");
    if (code == 200) {
        ESP.restart();
    }
}

boolean IoTGuru::firmwareUpdate(const char* ota_version) {
    IOTGURU_DEBUG_PRINT("ENTRY");
#if defined(ESP8266)
    String updateUrl = String(IOT_GURU_BASE_URL) + "firmware/update/" + this->deviceKey + "/" + ota_version;
    IOTGURU_DEBUG_PRINT("Send request to the cloud: " + updateUrl);

    t_httpUpdate_return ret = ESPhttpUpdate.update(String(IOT_GURU_BASE_HOST), 80, updateUrl, String(ota_version), false, "", false);
    switch(ret) {
        case HTTP_UPDATE_FAILED: {
            IOTGURU_DEBUG_PRINT("HTTP update: failed(" + String(ESPhttpUpdate.getLastError()) + "): " + ESPhttpUpdate.getLastErrorString());
            break;
        }
        case HTTP_UPDATE_NO_UPDATES: {
            IOTGURU_DEBUG_PRINT("HTTP update: no updates");
            break;
        }
        case HTTP_UPDATE_OK: {
            IOTGURU_DEBUG_PRINT("HTTP update: OK");
            ESP.restart();
            break;
        }
    }
#endif
#if defined(ESP32)
    String updateUrl = "/firmware/update/" + this->deviceKey + "/" + ota_version;
    IOTGURU_DEBUG_PRINT("Send request to the cloud: " + updateUrl);

    int contentLength = 0;
    bool isValidContentType = false;
    if (this->wiFiClient.connect(IOT_GURU_BASE_HOST, 80)) {
        this->wiFiClient.print(String("GET ") + updateUrl + " HTTP/1.1\r\n" +
                "Host: " + IOT_GURU_BASE_HOST + "\r\n" +
                "Cache-Control: no-cache\r\n" +
                "Connection: close\r\n\r\n");

        unsigned long timeout = millis();
        while (this->wiFiClient.available() == 0) {
            delay(1);
            if (millis() - timeout > 5000) {
                IOTGURU_DEBUG_PRINT("Client Timeout!");
                this->wiFiClient.stop();
                return false;
            }
        }

        while (this->wiFiClient.available()) {
            String line = this->wiFiClient.readStringUntil('\n');
            line.trim();

            if (!line.length()) {
                break;
            }

            if (line.startsWith("HTTP/1.1")) {
                if (line.indexOf("200") < 0) {
                    IOTGURU_DEBUG_PRINT("Got a non 200 status code from server (" + line + "). Exiting OTA Update.");
                    break;
                }
            }

            if (line.startsWith("Content-Length: ")) {
                contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
                IOTGURU_DEBUG_PRINT("Got " + String(contentLength) + " bytes from server");
            }

            if (line.startsWith("Content-Type: ")) {
                String contentType = getHeaderValue(line, "Content-Type: ");
                IOTGURU_DEBUG_PRINT("Got " + contentType + " payload.");
                if (contentType == "application/octet-stream; charset=UTF-8") {
                    isValidContentType = true;
                }
            }
        }
    } else {
        IOTGURU_DEBUG_PRINT("Connection to " + String(IOT_GURU_BASE_HOST) + " failed. Please check your setup");
    }

    if (!contentLength || !isValidContentType) {
        this->wiFiClient.stop();
        return false;
    }

    bool canBegin = Update.begin(contentLength);
    if (!canBegin) {
        IOTGURU_DEBUG_PRINT("There is no enough space to begin OTA... giving up.");
        this->wiFiClient.stop();
        return false;
    }

    Serial.println("Begin OTA update...");
    size_t written = Update.writeStream(wiFiClient);
    if (written == contentLength) {
        IOTGURU_DEBUG_PRINT("Written: " + String(written) + " successfully.");
    } else {
        IOTGURU_DEBUG_PRINT("Written only: " + String(written) + "/" + String(contentLength) + "...");
    }

    if (Update.end()) {
        IOTGURU_DEBUG_PRINT("OTA process ended...");
        if (Update.isFinished()) {
            IOTGURU_DEBUG_PRINT("Update successfully completed. Rebooting.");
            ESP.restart();
        } else {
            IOTGURU_DEBUG_PRINT("Update not finished? Something went wrong! Uh-oh...");
        }
    } else {
        IOTGURU_DEBUG_PRINT("Error Occurred. Error code is " + String(Update.getError()));
    }
#endif
    IOTGURU_DEBUG_PRINT("EXIT");

    return true;
}

boolean IoTGuru::loop() {
    this->mqttClient.loop();

    if (mqttLastConnected == 0 || mqttLastConnected + mqttReconnectDuration < millis()) {
        mqttLastConnected = millis();
    } else {
        return false;
    }

    this->mqttConnect();

    return true;
}

boolean IoTGuru::sendHttpValue(String nodeShortId, String fieldName, float value) {
    IOTGURU_DEBUG_PRINT("ENTRY");

    IOTGURU_DEBUG_PRINT("Send request to the cloud");
    HTTPClient httpClient;
    httpClient.useHTTP10(true);
    httpClient.setTimeout(1000);

    httpClient.begin(String(IOT_GURU_BASE_URL) + "measurement/create/" + nodeShortId + "/" + fieldName + "/" + String(value));
    int code = httpClient.GET();
    httpClient.end();

    IOTGURU_DEBUG_PRINT("Response received from the cloud (status code " + String(code) + ")");

    IOTGURU_DEBUG_PRINT("EXIT");
    return code == 200;
}

boolean IoTGuru::sendMqttValue(String nodeShortId, String fieldName, float value) {
    IOTGURU_DEBUG_PRINT("ENTRY");

    IOTGURU_DEBUG_PRINT("Send request to the cloud");
    String topic = String("pub/" + this->userShortId + "/" + this->deviceShortId + "/" + nodeShortId + "/" + fieldName);
    boolean result = this->mqttClient.publish(topic.c_str(), String(value).c_str());

    IOTGURU_DEBUG_PRINT("Response received from the cloud (status " + String(result) + ")");

    IOTGURU_DEBUG_PRINT("EXIT");
    return result;
}

void IoTGuru::debugPrint(String function, int line, String msg) {
    if (this->debugPrinter) {
        debugPrinter->print(millis());
        debugPrinter->print(": {");
        debugPrinter->print(function);
        debugPrinter->print(":");
        debugPrinter->print(line);
        debugPrinter->print("} - ");
        debugPrinter->println(msg);
    }
}

boolean IoTGuru::mqttConnect() {
    if (mqttClient.isConnected()) {
       return true;
    }

    IOTGURU_DEBUG_PRINT("ENTRY");
    IOTGURU_DEBUG_PRINT("Send MQTT connection request to the cloud");

    IOTGURU_DEBUG_PRINT("MQTT clientId: " + this->deviceShortId);
    mqttClient.setServer(IOT_GURU_MQTT_HOST, 1883);
    mqttClient.setCallback([this] (char* topic, byte* payload, unsigned int length) { this->mqttCallback(topic, payload, length); });

    if (mqttClient.connect(this->deviceShortId.c_str(), this->userShortId.c_str(), this->deviceKey.c_str())) {
        String topic = String("sub/" + this->userShortId + "/" + this->deviceShortId + "/#");
        mqttClient.subscribe(topic.c_str());

        IOTGURU_DEBUG_PRINT("Connected and subscribed to the '" + topic + "' topic.");

        IOTGURU_DEBUG_PRINT("EXIT");
        return true;
    }

    IOTGURU_DEBUG_PRINT("Connection failed, rc=" + String(mqttClient.getState()));

    IOTGURU_DEBUG_PRINT("EXIT");
    return false;
}

boolean IoTGuru::mqttCallback(char* topicChars, byte* payloadBytes, unsigned int length) {
    IOTGURU_DEBUG_PRINT("ENTRY");

    char payloadChars[length + 1];
    for (int i = 0; i < length; i++) {
        payloadChars[i] = (char)payloadBytes[i];
    }
    payloadChars[length] = '\0';

    String topic = String(topicChars);
    String payload = String(payloadChars);

    IOTGURU_DEBUG_PRINT("MQTT payload [" + payload + "] arrived on the [" + topic + "] topic");

    String nodeShortId;
    String fieldName;

    int start = 0;
    int parts = 0;
    for (int end = 0; end < topic.length(); end++) {
        if (topic.charAt(end) == '/') {
            if (parts == 3) {
                nodeShortId = topic.substring(start, end);
            }

            start = end + 1;
            parts++;
        }
    }
    if (start < topic.length()) {
        if (parts == 4) {
            fieldName = topic.substring(start, topic.length());
        }
    }

    if (nodeShortId.length() && fieldName.length()) {
        IOTGURU_DEBUG_PRINT("ENTRY callback");
        this->callback(nodeShortId.c_str(), fieldName.c_str(), payload.c_str());
        IOTGURU_DEBUG_PRINT("EXIT callback");
    } else {
        IOTGURU_DEBUG_PRINT("Invalid MQTT topic structure");
    }

    IOTGURU_DEBUG_PRINT("EXIT");
    return true;
}

String IoTGuru::getHeaderValue(String header, String headerName) {
    return header.substring(strlen(headerName.c_str()));
}
