#!/usr/bin/env python3
"""
serial_qos2_broker.py

Serial-based MQTT-QoS2 "broker" + subscriber for Arduino demo.

Usage:
    python3 serial_qos2_broker.py --port /dev/ttyACM0
or
    python3 serial_qos2_broker.py   # auto-detects first serial port

Requires: pyserial
    pip install pyserial
"""

import serial
import time
import sys
import json
import argparse
import os

PROCESSED_FILE = "processed_ids.json"
BAUD = 115200
DEFAULT_TIMEOUT = 0.1

def list_ports():
    try:
        from serial.tools import list_ports
        return [p.device for p in list_ports.comports()]
    except Exception:
        return []

def load_processed():
    if os.path.exists(PROCESSED_FILE):
        try:
            with open(PROCESSED_FILE, "r") as f:
                return set(json.load(f))
        except Exception:
            return set()
    return set()

def save_processed(s):
    try:
        with open(PROCESSED_FILE, "w") as f:
            json.dump(sorted(list(s)), f)
    except Exception as e:
        print("Err saving processed ids:", e)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", "-p", help="Serial port (e.g. /dev/ttyACM0 or COM3)")
    args = ap.parse_args()

    port = args.port
    if not port:
        ports = list_ports()
        if not ports:
            print("No serial ports found. Please provide --port")
            sys.exit(1)
        port = ports[0]
        print("Auto-detected serial port:", port)

    try:
        ser = serial.Serial(port, BAUD, timeout=DEFAULT_TIMEOUT)
    except Exception as e:
        print("Failed to open serial port", port, ":", e)
        sys.exit(1)

    # allow Arduino to reset and settle
    time.sleep(2.0)
    print("Broker listening on", port)

    processed = load_processed()  # set of message-id strings
    # We'll treat msg_id as string keys for JSON persistence

    def process_publish(msg_id_str, payload_str):
        # Try to parse JSON payload; if JSON contains "alert" print special, else treat as distance or raw
        try:
            obj = json.loads(payload_str)
            alert = obj.get("alert")
            if alert:
                # ALERT payload structure used by threshold sketch
                print("ALERT:", alert, "id", msg_id_str, "dist", obj.get("dist"))
            else:
                # generic JSON payload
                print("Process NEW msg:", msg_id_str, "data:", obj)
        except Exception:
            # not JSON, assume distance or raw string
            print("Process NEW msg:", msg_id_str, "distance:", payload_str)

        # mark processed (idempotent)
        processed.add(msg_id_str)
        save_processed(processed)

    try:
        while True:
            try:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode(errors="ignore").strip()
                if not line:
                    continue
                print("RX:", line)

                parts = line.split("|", 2)  # allow payload to contain '|'
                cmd = parts[0].strip().upper()

                if cmd == "PUBLISH" and len(parts) >= 3:
                    msg_id = parts[1].strip()
                    payload = parts[2].strip()
                    # Immediately acknowledge receipt (PUBREC)
                    out = f"PUBREC|{msg_id}\n"
                    ser.write(out.encode())
                    print("TX:", out.strip())

                    # Process if not already processed
                    if msg_id in processed:
                        print("Already processed", msg_id)
                    else:
                        process_publish(msg_id, payload)

                    # Important: per QoS2 semantics, broker waits for PUBREL then sends PUBCOMP.
                    # We do not send PUBCOMP now; wait for PUBREL from client.

                elif cmd == "PUBREL" and len(parts) >= 2:
                    msg_id = parts[1].strip()
                    # On PUBREL, send PUBCOMP to finish handshake
                    out = f"PUBCOMP|{msg_id}\n"
                    ser.write(out.encode())
                    print("TX:", out.strip())

                else:
                    # ignore unknown or additional commands but print for debug
                    # For example Arduino prints "Sent PUBLISH id ...", etc.
                    # We already printed RX line above, so nothing else required.
                    pass

            except KeyboardInterrupt:
                print("^CExiting")
                break
            except Exception as e:
                # keep broker alive on transient errors
                print("Err in loop:", e)
                time.sleep(0.1)
    finally:
        try:
            ser.close()
        except:
            pass

if __name__ == "__main__":
    main()

