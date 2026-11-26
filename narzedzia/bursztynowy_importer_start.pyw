#!/usr/bin/env python3
"""Bursztynowy Importer Plikow - uruchamianie dwuklikiem (Windows: .pyw bez okna konsoli) lub: python3 <ten plik>."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bursztynowy_importer.gui import main  # noqa: E402

main()
