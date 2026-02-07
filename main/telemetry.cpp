#include "telemetry.h"
#include "modem.h"
#include "config_user.h"
#include "logs.h"

extern SemaphoreHandle_t modemMutex;

void initTelemetry() {
    // Ya se inicializa el módem en setup
}

bool sendTelemetry(const TelemetryData& data) {
    if (!gprsConnected) return false;

    if (modemMutex && xSemaphoreTake(modemMutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
        logMsg(LOG_WARN, "TELEMETRY", "No se pudo tomar el mutex del módem");
        return false;
    }

    bool success = false;
    
    // Parsear URL para obtener host y path
    String url = WEB_PORTAL_URL;
    int protocolEnd = url.indexOf("://");
    String hostPath = (protocolEnd == -1) ? url : url.substring(protocolEnd + 3);
    int pathStart = hostPath.indexOf("/");
    String host = (pathStart == -1) ? hostPath : hostPath.substring(0, pathStart);
    String path = (pathStart == -1) ? "/" : hostPath.substring(pathStart);

    logMsg(LOG_INFO, "TELEMETRY", "Conectando a " + host);
    
    secureClient.stop();
    if (secureClient.connect(host.c_str(), 443)) {
        String body = "{";
        body += "\"motorcycle_id\":\"" + String(VEHICLE_ID) + "\",";
        body += "\"latitude\":" + String(data.lat, 6) + ",";
        body += "\"longitude\":" + String(data.lng, 6) + ",";
        body += "\"speed\":" + String(data.speed, 1) + ",";
        body += "\"battery_level\":" + String(data.soc) + ",";
        body += "\"battery_voltage\":" + String(data.voltage, 1) + ",";
        body += "\"is_charging\":" + String(data.current > 0 ? "true" : "false");
        body += "}";

        String request = "POST " + path + " HTTP/1.1\r\n";
        request += "Host: " + host + "\r\n";
        request += "Content-Type: application/json\r\n";
        request += "x-api-key: " + String(TELEMETRY_SECRET) + "\r\n";
        request += "Content-Length: " + String(body.length()) + "\r\n";
        request += "Connection: close\r\n\r\n";
        request += body;

        secureClient.print(request);

        // Esperar respuesta brevemente
        unsigned long timeout = millis();
        while (secureClient.connected() && millis() - timeout < 5000) {
            if (secureClient.available()) {
                String line = secureClient.readStringUntil('\n');
                if (line.indexOf("200 OK") != -1) {
                    success = true;
                }
                // Podríamos leer el resto pero con el 200 nos vale
                if (line == "\r") break;
            }
        }
        secureClient.stop();
    } else {
        logMsg(LOG_ERROR, "TELEMETRY", "Error de conexión HTTPS");
    }

    if (modemMutex) xSemaphoreGive(modemMutex);
    
    if (success) {
        logMsg(LOG_INFO, "TELEMETRY", "Datos enviados OK");
    } else {
        logMsg(LOG_WARN, "TELEMETRY", "Fallo al enviar datos");
    }
    
    return success;
}
