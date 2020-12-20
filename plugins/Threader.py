#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Threader: 用于测试多线程的模块"""
import random
import threading
import time
from concurrent.futures import ThreadPoolExecutor

executor = ThreadPoolExecutor(10)
lock = threading.Lock()
counter = 0


def incr(value: int):
    global counter
    # sleep 0 - 0.0001 second
    time.sleep(random.random() / 10000)
    with lock:
        counter += value


def incr_in_thread(value: int):
    threading.Thread(target=incr, args=(value,)).start()


def incr_in_executor(value: int):
    executor.submit(incr, value)
