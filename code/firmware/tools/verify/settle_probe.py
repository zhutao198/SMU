import urllib.request, time, json

BASE = "http://192.168.7.200"
def get(path):
    req = urllib.request.Request(BASE+path, headers={"Connection":"close"})
    with urllib.request.urlopen(req, timeout=3) as r:
        return r.read().decode()

# 先稳定在 5V
get("/set?vset=5"); time.sleep(0.6)
print("before step: vmeas=%.4f" % json.loads(get("/data.json"))["vmeas"])
# 阶跃到 10V
get("/set?vset=10")
t0 = time.time()
for i in range(14):
    try:
        d = json.loads(get("/data.json"))
        print("t=%.3fs vmeas=%.4f" % (time.time()-t0, d["vmeas"]))
    except Exception as e:
        print("t=%.3fs err %s" % (time.time()-t0, e))
    time.sleep(0.07)
