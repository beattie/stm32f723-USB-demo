#!/usr/bin/env python3
import getpass

username = input("Username: ")
password = getpass.getpass("Password: ")
print(f"\nGot: username={username!r}, password={'*' * len(password)}")
