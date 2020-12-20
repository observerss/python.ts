#!/usr/bin/env
# -*- coding: utf-8 -*-
import multiprocessing as mp
import random
import time
from concurrent.futures import ProcessPoolExecutor

if mp.get_start_method() == "fork":
    methods = mp.get_all_start_methods()
    mp.set_start_method("forkserver" if "forkserver" in methods else "spawn", force=True)
executor = ProcessPoolExecutor(2)


def run():
    wait = random.random() / 10000
    time.sleep(wait)
    return wait


def run_in_executor():
    res = executor.submit(run)
    return res.result()
