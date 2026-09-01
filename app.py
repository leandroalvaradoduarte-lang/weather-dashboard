"""
Estación Meteorológica - Servidor local
----------------------------------------
Recibe lecturas del ESP32 (presión) y, opcionalmente, del Arduino
(temperatura/humedad si se reenvían por serial/Bluetooth al ESP32),
las guarda en SQLite y expone una API + página web con historial.

Ejecutar:
    pip install -r requirements.txt
    python app.py

El servidor escucha en el puerto 5000, accesible desde cualquier
dispositivo de la misma red en http://<IP-de-esta-PC>:5000
"""

import sqlite3
import os
from datetime import datetime, timedelta
from flask import Flask, request, jsonify, render_template, g
# LED indicators active - dynamic weather dashboard with real-time updates 2026

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(BASE_DIR, "station.db")
DATABASE_URL = os.getenv("DATABASE_URL")

app = Flask(__name__)

# ---------------------------------------------------------------------------
# Umbrales de alerta (igual que en el firmware del ESP32)
# ---------------------------------------------------------------------------
UMBRAL_PRECAUCION = 0.5   # hPa en 1 hora -> LED amarillo
UMBRAL_ALERTA = 1.5       # hPa en intervalo breve -> LED rojo
UMBRAL_ALERTA_3H = 4.0    # hPa acumulado en 3 horas -> LED rojo


def get_db():
    if "db" not in g:
        if DATABASE_URL:
            import psycopg
            from psycopg.rows import dict_row

            g.db = psycopg.connect(DATABASE_URL, row_factory=dict_row)
        else:
            g.db = sqlite3.connect(DB_PATH)
            g.db.row_factory = sqlite3.Row
    return g.db


def execute(db, query, parameters=()):
    if DATABASE_URL:
        query = query.replace("?", "%s")
    return db.execute(query, parameters)


@app.teardown_appcontext
def close_db(exception=None):
    db = g.pop("db", None)
    if db is not None:
        db.close()


def init_db():
    if DATABASE_URL:
        import psycopg
        conn = psycopg.connect(DATABASE_URL)
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS readings (
                id BIGSERIAL PRIMARY KEY,
                timestamp TEXT NOT NULL,
                pressure_hpa DOUBLE PRECISION,
                temperature_c DOUBLE PRECISION,
                humidity_pct DOUBLE PRECISION,
                alert_level TEXT
            )
            """
        )
        conn.execute("CREATE INDEX IF NOT EXISTS idx_readings_timestamp ON readings(timestamp)")
    else:
        conn = sqlite3.connect(DB_PATH)
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                pressure_hpa REAL,
                temperature_c REAL,
                humidity_pct REAL,
                alert_level TEXT
            )
            """
        )
        conn.execute("CREATE INDEX IF NOT EXISTS idx_readings_timestamp ON readings(timestamp)")
    conn.commit()
    conn.close()


def calcular_alerta(db, pressure_hpa):
    """Calcula el nivel de alerta comparando con la lectura anterior
    y con la variación acumulada en las últimas 3 horas, replicando
    la lógica del firmware del ESP32."""
    if pressure_hpa is None:
        return None

    ultima = execute(db,
        "SELECT pressure_hpa FROM readings WHERE pressure_hpa IS NOT NULL "
        "ORDER BY id DESC LIMIT 1"
    ).fetchone()

    nivel = "estable"

    if ultima is not None and ultima["pressure_hpa"] is not None:
        diferencia = abs(pressure_hpa - ultima["pressure_hpa"])
        if diferencia >= UMBRAL_ALERTA:
            nivel = "alerta"
        elif diferencia >= UMBRAL_PRECAUCION:
            nivel = "precaucion"

    # Variación acumulada en las últimas 3 horas
    hace_3h = (datetime.utcnow() - timedelta(hours=3)).isoformat()
    referencia = execute(db,
        "SELECT pressure_hpa FROM readings WHERE timestamp <= ? "
        "AND pressure_hpa IS NOT NULL ORDER BY timestamp DESC LIMIT 1",
        (hace_3h,),
    ).fetchone()
    if referencia is not None and referencia["pressure_hpa"] is not None:
        variacion_3h = abs(pressure_hpa - referencia["pressure_hpa"])
        if variacion_3h >= UMBRAL_ALERTA_3H:
            nivel = "alerta"

    return nivel


@app.route("/")
def index():
    from flask import Response
    html = render_template("index.html")
    resp = Response(html, mimetype='text/html')
    resp.headers['Cache-Control'] = 'no-cache, no-store, must-revalidate, max-age=0'
    resp.headers['Pragma'] = 'no-cache'
    resp.headers['Expires'] = '0'
    return resp


@app.route("/api/reading", methods=["POST"])
def post_reading():
    """Endpoint que llama el ESP32 (y opcionalmente el Arduino) para
    enviar una lectura nueva.

    Cuerpo JSON esperado (todos los campos son opcionales, manda lo
    que tengas disponible en cada envío):
    {
        "pressure_hpa": 1012.4,
        "temperature_c": 29.1,
        "humidity_pct": 57.0
    }
    """
    data = request.get_json(silent=True) or {}

    pressure = data.get("pressure_hpa")
    temperature = data.get("temperature_c")
    humidity = data.get("humidity_pct")

    if pressure is None and temperature is None and humidity is None:
        return jsonify({"error": "Envía al menos un valor: pressure_hpa, temperature_c o humidity_pct"}), 400

    db = get_db()
    alert_level = calcular_alerta(db, pressure) if pressure is not None else data.get("alert_level")

    execute(db,
        "INSERT INTO readings (timestamp, pressure_hpa, temperature_c, humidity_pct, alert_level) "
        "VALUES (?, ?, ?, ?, ?)",
        (datetime.utcnow().isoformat(), pressure, temperature, humidity, alert_level),
    )
    db.commit()

    return jsonify({"status": "ok", "alert_level": alert_level}), 201


@app.route("/api/latest")
def get_latest():
    db = get_db()
    row = execute(db,
        "SELECT * FROM readings ORDER BY id DESC LIMIT 1"
    ).fetchone()
    if row is None:
        return jsonify(None)

    latest = dict(row)
    for field in ("pressure_hpa", "temperature_c", "humidity_pct"):
        if latest[field] is None:
            previous = execute(db,
                f"SELECT {field} FROM readings WHERE {field} IS NOT NULL "
                "ORDER BY id DESC LIMIT 1"
            ).fetchone()
            latest[field] = previous[field] if previous is not None else None

    return jsonify(latest)


RANGE_TO_DELTA = {
    "5m": timedelta(minutes=5),
    "6h": timedelta(hours=6),
    "12h": timedelta(hours=12),
    "1d": timedelta(days=1),
    "1w": timedelta(weeks=1),
}


@app.route("/api/history")
def get_history():
    range_key = request.args.get("range", "5m")
    delta = RANGE_TO_DELTA.get(range_key, timedelta(minutes=5))
    desde = (datetime.utcnow() - delta).isoformat()

    db = get_db()
    rows = execute(db,
        "SELECT * FROM readings WHERE timestamp >= ? ORDER BY timestamp ASC",
        (desde,),
    ).fetchall()
    return jsonify([dict(r) for r in rows])


if __name__ == "__main__":
    init_db()
    # host="0.0.0.0" para que el ESP32 (en la misma red) pueda alcanzar el servidor
    print("=" * 60)
    print("SERVIDOR METEOROLOGICO ACTIVO")
    print("Dashboard: http://127.0.0.1:5000")
    print("Red local: http://10.10.93.55:5000")
    print("API ESP32: http://10.10.93.55:5000/api/reading")
    print("Esperando lecturas de la ESP32...")
    print("Presiona CTRL+C para detener el servidor")
    print("=" * 60)
    app.run(host="0.0.0.0", port=5000, debug=True, use_reloader=False)
