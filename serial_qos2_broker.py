# serial_qos2_broker.py
import serial, time
import sys

# set port accordingly, e.g. "COM3" on Windows or "/dev/ttyACM0" on Linux
PORT = "/dev/ttyACM0"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=0.1)
time.sleep(2)  # wait for Arduino reset

print("Broker listening on", PORT)

# simple processed storage to avoid duplicate processing (in-memory)
processed = set()

def process_publish(msg_id, dist):
    if msg_id in processed:
        print("Already processed", msg_id)
    else:
        print("Process NEW msg:", msg_id, "distance:", dist)
        processed.add(msg_id)
    # after processing, broker waits for PUBREL then sends PUBCOMP (handled in main)

while True:
    try:
        line = ser.readline().decode(errors='ignore').strip()
        if not line:
            continue
        print("RX:", line)
        parts = line.split("|")
        if parts[0] == "PUBLISH" and len(parts) >= 3:
            msg_id = int(parts[1])
            dist = parts[2]
            # send PUBREC immediately to ack receipt
            out = f"PUBREC|{msg_id}\n"
            ser.write(out.encode())
            print("TX:", out.strip())
            # process (but do not send PUBCOMP until PUBREL arrives)
            process_publish(msg_id, dist)
        elif parts[0] == "PUBREL" and len(parts) >= 2:
            msg_id = int(parts[1])
            # finalise: send PUBCOMP
            out = f"PUBCOMP|{msg_id}\n"
            ser.write(out.encode())
            print("TX:", out.strip())
        else:
            # ignore unknown
            pass
    except KeyboardInterrupt:
        print("Exiting")
        ser.close()
        sys.exit(0)
    except Exception as e:
        print("Err:", e)

