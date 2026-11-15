
import os
import platform
import time
from datetime import datetime

import psutil


def to_gb(value):
    return value / (1024 ** 3)


def show_system_info():
    print("\n========== ShellSense AI: SYSTEM MONITOR ==========")

    print(f"Time       : {datetime.now()}")
    print(f"OS         : {platform.system()} {platform.release()}")
    print(f"Architecture: {platform.machine()}")
    print(f"CPU cores  : {psutil.cpu_count(logical=False)} physical, "
          f"{psutil.cpu_count(logical=True)} logical")

    # CPU usage
    print("\n--- CPU ---")
    print(f"CPU usage  : {psutil.cpu_percent(interval=1)}%")

    # Memory
    memory = psutil.virtual_memory()

    print("\n--- MEMORY ---")
    print(f"Total RAM  : {to_gb(memory.total):.2f} GB")
    print(f"Used RAM   : {to_gb(memory.used):.2f} GB")
    print(f"Available  : {to_gb(memory.available):.2f} GB")
    print(f"RAM usage  : {memory.percent}%")

    swap = psutil.swap_memory()
    print(f"Swap usage : {swap.percent}%")

    # Disk
    print("\n--- DISK ---")
    disk = psutil.disk_usage("/")

    print(f"Total      : {to_gb(disk.total):.2f} GB")
    print(f"Used       : {to_gb(disk.used):.2f} GB")
    print(f"Free       : {to_gb(disk.free):.2f} GB")
    print(f"Disk usage : {disk.percent}%")

    # Network
    print("\n--- NETWORK ---")
    network = psutil.net_io_counters()

    print(f"Bytes sent : {to_gb(network.bytes_sent):.4f} GB")
    print(f"Bytes recv : {to_gb(network.bytes_recv):.4f} GB")
    print(f"Packets sent: {network.packets_sent}")
    print(f"Packets recv: {network.packets_recv}")

    # Process CPU sampling
    print("\n--- TOP PROCESSES ---")

    processes = []

    for process in psutil.process_iter(
        ["pid", "name", "memory_percent"]
    ):
        try:
            process.cpu_percent()
            processes.append(process)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue

    time.sleep(0.5)

    process_data = []

    for process in processes:
        try:
            process_data.append({
                "pid": process.pid,
                "name": process.name(),
                "cpu": process.cpu_percent(),
                "memory": process.memory_percent()
            })
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue

    process_data.sort(
        key=lambda item: item["cpu"],
        reverse=True
    )

    print(f"{'PID':<8}{'PROCESS':<25}{'CPU %':>10}{'MEM %':>10}")
    print("-" * 53)

    for process in process_data[:10]:
        print(
            f"{process['pid']:<8}"
            f"{process['name'][:24]:<25}"
            f"{process['cpu']:>10.1f}"
            f"{process['memory']:>10.1f}"
        )

    print("===================================================\n")


if __name__ == "__main__":
    try:
        show_system_info()
    except KeyboardInterrupt:
        print("\nMonitoring interrupted.")
    except Exception as error:
        print(f"Monitoring error: {error}")
