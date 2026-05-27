#!/usr/bin/env python3
"""生成合成 OHLCV CSV，用于回测系统测试。模型：几何布朗运动。"""

import csv, math, random, os
from datetime import date, timedelta


def gen_ohlcv(n=504, s0=150.0, mu=0.0003, sigma=0.015, seed=42):
    random.seed(seed)
    rows = []
    price = s0
    d = date(2020, 1, 2)
    for _ in range(n):
        # 跳过周末
        while d.weekday() >= 5:
            d += timedelta(1)
        ret = random.gauss(mu, sigma)
        close = round(price * math.exp(ret), 2)
        hi = round(max(price, close) * (1 + abs(random.gauss(0, sigma / 2))), 2)
        lo = round(min(price, close) * (1 - abs(random.gauss(0, sigma / 2))), 2)
        op = round(price * (1 + random.gauss(0, sigma / 3)), 2)
        vol = random.randint(5_000_000, 50_000_000)
        rows.append((d.isoformat(), op, hi, lo, close, vol))
        price = close
        d += timedelta(1)
    return rows


os.makedirs("data/sample", exist_ok=True)
rows = gen_ohlcv()
with open("data/sample/AAPL.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["date", "open", "high", "low", "close", "volume"])
    w.writerows(rows)
print(f"Generated {len(rows)} bars → data/sample/AAPL.csv")
