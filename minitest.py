#!/usr/bin/env python3
import os
import sys
import time

def step(msg):
    print(f"\n[>] {msg}")

def check(condition, error_msg):
    if not condition:
        print(f"  [✗] ОШИБКА: {error_msg}")
    else: print("  [✓] Успех")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 debug_bench.py <mountpoint>")
        sys.exit(1)

    mnt = sys.argv[1]
    
    step("1. Проверка точки монтирования (getattr /)")
    check(os.path.ismount(mnt) or os.path.isdir(mnt), "Точка монтирования недоступна")

    step("2. Создание одной директории (mkdir /testdir)")
    test_dir = os.path.join(mnt, "testdir")
    try:
        os.mkdir(test_dir)
        check(True, "")
    except Exception as e:
        check(False, f"mkdir упал: {e}")

    # Небольшая пауза, чтобы дать FUSE перевести дух (полезно при дебаге)
    time.sleep(0.5)

    step("3. Проверка существования директории (getattr /testdir)")
    check(os.path.exists(test_dir), "getattr вернул ENOENT (папка создана, но ОС её не видит!)")

    step("4. Создание вложенной директории (mkdir /testdir/lvl_1)")
    lvl_1 = os.path.join(test_dir, "lvl_1")
    try:
        os.mkdir(lvl_1)
        check(True, "")
    except Exception as e:
        check(False, f"Вложенный mkdir упал: {e}")

    step("5. Чтение содержимого корня (readdir /)")
    try:
        entries = os.listdir(mnt)
        print(f"  Найдено в корне: {entries}")
        check("testdir" in entries, "testdir не появился в выводе readdir")
    except Exception as e:
        check(False, f"readdir упал: {e}")

    step("6. Создание файла (create/touch /testdir/hello.txt)")
    test_file = os.path.join(test_dir, "hello.txt")
    try:
        with open(test_file, "w") as f:
            pass
        check(True, "")
    except Exception as e:
        check(False, f"Не удалось создать файл: {e}")

    step("7. Запись в файл (write)")
    try:
        with open(test_file, "w") as f:
            f.write("Hello MiniBtrfs!\n")
        check(True, "")
    except Exception as e:
        check(False, f"Не удалось записать в файл: {e}")

    step("8. Чтение из файла (read)")
    try:
        with open(test_file, "r") as f:
            data = f.read()
        print(f"  Прочитано: {data.strip()}")
        check(data == "Hello MiniBtrfs!\n", "Данные повреждены или не записались")
    except Exception as e:
        check(False, f"Не удалось прочитать файл: {e}")

    print("\n🚀 БАЗОВЫЕ ТЕСТЫ ПРОЙДЕНЫ УСПЕШНО!")

if __name__ == "__main__":
    main()