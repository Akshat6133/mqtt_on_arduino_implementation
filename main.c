// qos2_serial_publisher_threshold.ino
#include <EEPROM.h>

const int trigPin = 9;
const int echoPin = 10;

unsigned long lastSend = 0;
unsigned long lastRetransmit = 0;
const unsigned long SEND_INTERVAL = 5000;
const unsigned long RETRANSMIT_INTERVAL = 3000;

// EEPROM layout
const int ADDR_LAST_ID = 0;       // 4 bytes
const int ADDR_PENDING_FLAG = 4;  // 1 byte
const int ADDR_PENDING_ID = 5;    // 4 bytes
const int ADDR_PENDING_DIST = 9;  // up to 20 bytes for payload string

// threshold params
const float THRESH = 10.0;      // threshold in cm
const float HYST = 0.5;         // hysteresis
enum State {UNKNOWN = 0, BELOW=1, ABOVE=2};
State lastState = UNKNOWN;

uint32_t readU32(int a){ uint32_t v=0; for(int i=0;i<4;i++) v |= ((uint32_t)EEPROM.read(a+i)) << (8*i); return v; }
void writeU32(int a,uint32_t v){ for(int i=0;i<4;i++) EEPROM.update(a+i,(v>>(8*i))&0xFF); }
bool hasPending(){ return EEPROM.read(ADDR_PENDING_FLAG)==1; }
void setPending(bool f){ EEPROM.update(ADDR_PENDING_FLAG, f?1:0); }
String readPendingDist(){ String s=""; for(int i=0;i<20;i++){ uint8_t b = EEPROM.read(ADDR_PENDING_DIST + i); if(b==0) break; s += char(b); } return s; }
void writePendingDist(const String &s){ for(int i=0;i<20;i++) EEPROM.update(ADDR_PENDING_DIST + i, i < (int)s.length() ? s[i] : 0); }

void setup() {
    Serial.begin(115200);
    pinMode(trigPin, OUTPUT); pinMode(echoPin, INPUT);
    if(readU32(ADDR_LAST_ID) == 0xFFFFFFFFUL) writeU32(ADDR_LAST_ID, 0);
    if(EEPROM.read(ADDR_PENDING_FLAG) > 1) setPending(false);
    Serial.println("Arduino QoS2 Serial Publisher (threshold) ready");
}

float readDistanceCm(){
    digitalWrite(trigPin, LOW); delayMicroseconds(2);
    digitalWrite(trigPin, HIGH); delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    unsigned long duration = pulseIn(echoPin, HIGH, 30000);
    if(duration == 0) return -1.0;
    return (duration * 0.0343) / 2.0;
}

void sendLine(const String &s){
    Serial.println(s);
}

void processSerial(){
    while(Serial.available()){
        String line = Serial.readStringUntil('\n');
        line.trim();
        if(line.length()==0) continue;
        if(line.startsWith("PUBREC|")){
            String idstr = line.substring(7);
            uint32_t id = (uint32_t)idstr.toInt();
            uint32_t pendId = readU32(ADDR_PENDING_ID);
            if(hasPending() && id == pendId){
                sendLine("PUBREL|" + String(id));
            }
        } else if(line.startsWith("PUBCOMP|")){
            String idstr = line.substring(8);
            uint32_t id = (uint32_t)idstr.toInt();
            uint32_t pendId = readU32(ADDR_PENDING_ID);
            if(hasPending() && id == pendId){
                setPending(false);
                writeU32(ADDR_PENDING_ID, 0);
                writePendingDist("");
                Serial.print("Completed msg "); Serial.println(id);
            }
        }
    }
}

bool shouldTrigger(float d){
    if(d < 0) return false;
    if(lastState == UNKNOWN){
        // set initial state without publishing immediately unless you want initial trigger
        if(d <= THRESH - HYST) lastState = BELOW;
        else if(d >= THRESH + HYST) lastState = ABOVE;
        // don't trigger on initial detection; change this if you want initial alert
        return false;
    }
    if(lastState == BELOW && d >= THRESH + HYST){
        lastState = ABOVE;
        return true;
    }
    if(lastState == ABOVE && d <= THRESH - HYST){
        lastState = BELOW;
        return true;
    }
    return false;
}

String buildAlertPayload(uint32_t id, float dist){
    // form "ALERT_HIGH:12.34" or "ALERT_LOW:8.50"
    char buf[80];
    const char* tag = (dist >= THRESH) ? "ALERT_HIGH" : "ALERT_LOW";
    snprintf(buf, sizeof(buf), "{\"msg_id\":%lu,\"alert\":\"%s\",\"dist\":%.2f}", (unsigned long)id, tag, dist);
    return String(buf);
}

void loop(){
    processSerial();

    // retransmit pending if exists
    if(hasPending()){
        if(millis() - lastRetransmit > RETRANSMIT_INTERVAL){
            uint32_t pid = readU32(ADDR_PENDING_ID);
            String pld = readPendingDist();
            sendLine("PUBLISH|" + String(pid) + "|" + pld);
            lastRetransmit = millis();
            Serial.print("Retransmit PUBLISH id "); Serial.println(pid);
        }
        return;
    }

    // periodic sensor read
    if(millis() - lastSend > SEND_INTERVAL){
        float d = readDistanceCm();
        lastSend = millis();
        if(d < 0){ Serial.println("No echo"); return; }

        // check whether to trigger
        if(shouldTrigger(d)){
            // produce new msg id and persist pending
            uint32_t lastId = readU32(ADDR_LAST_ID);
            lastId++;
            writeU32(ADDR_LAST_ID, lastId);
            String payload = buildAlertPayload(lastId, d);
            writeU32(ADDR_PENDING_ID, lastId);
            writePendingDist(payload);
            setPending(true);
            // send initial PUBLISH
            sendLine("PUBLISH|" + String(lastId) + "|" + payload);
            Serial.print("Sent ALERT PUBLISH id "); Serial.print(lastId); Serial.print(" payload "); Serial.println(payload);
        } else {
            // no trigger — do nothing (saves messages)
            Serial.print("No trigger (dist = "); Serial.print(d); Serial.println(")");
        }
    }
}
