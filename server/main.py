from fastapi import FastAPI, HTTPException, Query
from pydantic import BaseModel, Field
from typing import Optional, List
import oracledb
import logging
from datetime import datetime

# ── Logging ──────────────────────────────────────────────────
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger(__name__)

# ── Configuración Oracle ──────────────────────────────────────
DB_USER = "ControlAcceso"
DB_PASSWORD = "admin"
DB_DSN = "localhost:1521/XE"

# ── App ──────────────────────────────────────────────────────
app = FastAPI(
    title="Control de Acceso API",
    description="Servidor intermediario ESP32 ↔ Oracle (con deduplicación SPIFFS→Oracle)",
    version="2.3.0"
)

# ── Conexión helper ───────────────────────────────────────────
def get_connection():
    """Abre y devuelve una conexión a Oracle."""
    try:
        return oracledb.connect(user=DB_USER, password=DB_PASSWORD, dsn=DB_DSN)
    except oracledb.DatabaseError as e:
        log.error(f"Error conectando a Oracle: {e}")
        raise HTTPException(status_code=503, detail="No se pudo conectar a la base de datos")


def now_str() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def normalize_value(v):
    """Convierte CLOBs/LOBs a string para que FastAPI pueda serializar."""
    if hasattr(v, "read") and callable(v.read):
        return v.read()
    return v


def rows_to_dicts(cur):
    cols = [d[0].lower() for d in cur.description]
    rows = []
    for row in cur.fetchall():
        rows.append({cols[i]: normalize_value(val) for i, val in enumerate(row)})
    return rows


def fetch_one_dict(cur):
    row = cur.fetchone()
    if not row:
        return None
    cols = [d[0].lower() for d in cur.description]
    return {cols[i]: normalize_value(val) for i, val in enumerate(row)}


# ══════════════════════════════════════════════════════════════
# MODELOS
# ══════════════════════════════════════════════════════════════

class Alumno(BaseModel):
    rfid_uid: str
    name: str
    account: str
    materia: Optional[str] = None
    created_at: str


class AlumnoUpdate(BaseModel):
    rfid_uid: Optional[str] = None
    name: Optional[str] = None
    account: Optional[str] = None
    materia: Optional[str] = None
    created_at: Optional[str] = None


class Profesor(BaseModel):
    rfid_uid: str
    name: str
    account: str
    created_at: str


class ProfesorUpdate(BaseModel):
    rfid_uid: Optional[str] = None
    name: Optional[str] = None
    account: Optional[str] = None
    created_at: Optional[str] = None


class Materia(BaseModel):
    materia: str
    profesor: str
    created_at: Optional[str] = None


class MateriaUpdate(BaseModel):
    materia: Optional[str] = None
    profesor: Optional[str] = None
    created_at: Optional[str] = None


class ProfesorMateria(BaseModel):
    rfid_uid: str
    materia: str
    created_at: Optional[str] = None


class ProfesorMateriaUpdate(BaseModel):
    rfid_uid: Optional[str] = None
    materia: Optional[str] = None
    created_at: Optional[str] = None


class Horario(BaseModel):
    materia: str
    profesor: Optional[str] = None
    dia: str
    hora_inicio: str
    hora_fin: str
    created_at: Optional[str] = None


class HorarioUpdate(BaseModel):
    materia: Optional[str] = None
    profesor: Optional[str] = None
    dia: Optional[str] = None
    hora_inicio: Optional[str] = None
    hora_fin: Optional[str] = None
    created_at: Optional[str] = None


class Asistencia(BaseModel):
    timestamp: str
    rfid_uid: str
    name: str
    account: str
    materia: Optional[str] = None
    mode: str


class AsistenciaUpdate(BaseModel):
    timestamp: Optional[str] = None
    rfid_uid: Optional[str] = None
    name: Optional[str] = None
    account: Optional[str] = None
    materia: Optional[str] = None
    mode: Optional[str] = None


class AccesoDenegado(BaseModel):
    timestamp: str
    rfid_uid: str
    note: Optional[str] = None


class AccesoDenegadoUpdate(BaseModel):
    timestamp: Optional[str] = None
    rfid_uid: Optional[str] = None
    note: Optional[str] = None


class Notificacion(BaseModel):
    timestamp: str
    rfid_uid: str
    name: Optional[str] = None
    account: Optional[str] = None
    note: str
    leida: Optional[int] = 0


class NotificacionUpdate(BaseModel):
    timestamp: Optional[str] = None
    rfid_uid: Optional[str] = None
    name: Optional[str] = None
    account: Optional[str] = None
    note: Optional[str] = None
    leida: Optional[int] = None


class SyncPayload(BaseModel):
    asistencias: List[Asistencia] = Field(default_factory=list)
    accesos_denegados: List[AccesoDenegado] = Field(default_factory=list)
    notificaciones: List[Notificacion] = Field(default_factory=list)


# ══════════════════════════════════════════════════════════════
# HELPERS DE DEDUPLICACIÓN
#
# PROBLEMA 1 - ORA-00936 (falta expresión):
#   TIMESTAMP y MODE son palabras reservadas en Oracle.
#   Sin comillas dobles Oracle no las reconoce como columnas.
#   SOLUCIÓN: usar alias de tabla (as_, ad_, nt_) y referenciar
#   como alias.columna en el WHERE.
#
# PROBLEMA 2 - ORA-00932 (tipo CLOB inconsistente):
#   Las columnas 'note' son CLOB y no se pueden comparar con =.
#   SOLUCIÓN: envolver con TO_CHAR() en el WHERE.
# ══════════════════════════════════════════════════════════════

def _insert_asistencia_idempotent(cur, a: Asistencia) -> bool:
    """
    Inserta asistencia solo si no existe ya una con mismo
    timestamp + rfid_uid + mode.
    Retorna True si se insertó, False si ya existía (skip).
    """
    cur.execute("""
        SELECT COUNT(*) FROM asistencias
        WHERE fecha_hora = :1 AND rfid_uid = :2 AND tipo = :3
    """, [a.timestamp, a.rfid_uid, a.mode])
    if cur.fetchone()[0] > 0:
        return False

    cur.execute("""
        INSERT INTO asistencias (fecha_hora, rfid_uid, name, account, materia, tipo)
        VALUES (:1, :2, :3, :4, :5, :6)
    """, [a.timestamp, a.rfid_uid, a.name, a.account, a.materia, a.mode])
    return True


def _insert_denegado_idempotent(cur, d: AccesoDenegado) -> bool:
    """
    Inserta acceso denegado solo si no existe ya uno con mismo
    timestamp + rfid_uid + note.
    note puede ser CLOB → se usa TO_CHAR() para comparar.
    """
    note_val = d.note or ""
    cur.execute("""
        SELECT COUNT(*) FROM accesos_denegados ad_
        WHERE ad_.timestamp = :1 AND ad_.rfid_uid = :2
          AND TO_CHAR(ad_.note) = :3
    """, [d.timestamp, d.rfid_uid, note_val])
    if cur.fetchone()[0] > 0:
        return False

    cur.execute("""
        INSERT INTO accesos_denegados (timestamp, rfid_uid, note)
        VALUES (:1, :2, :3)
    """, [d.timestamp, d.rfid_uid, d.note])
    return True


def _insert_notificacion_idempotent(cur, n: Notificacion) -> bool:
    """
    Inserta notificación solo si no existe ya una con mismo
    timestamp + rfid_uid + note.
    note es CLOB → se usa TO_CHAR() para comparar.
    """
    cur.execute("""
        SELECT COUNT(*) FROM notificaciones nt_
        WHERE nt_.timestamp = :1 AND nt_.rfid_uid = :2
          AND TO_CHAR(nt_.note) = :3
    """, [n.timestamp, n.rfid_uid, n.note])
    if cur.fetchone()[0] > 0:
        return False

    cur.execute("""
        INSERT INTO notificaciones (timestamp, rfid_uid, name, account, note, leida)
        VALUES (:1, :2, :3, :4, :5, :6)
    """, [n.timestamp, n.rfid_uid, n.name, n.account, n.note, n.leida or 0])
    return True


# ══════════════════════════════════════════════════════════════
# ENDPOINTS BASE
# ══════════════════════════════════════════════════════════════

@app.get("/ping", summary="Health check para el ESP32")
def ping():
    return {"status": "ok", "timestamp": datetime.now().isoformat()}


# ══════════════════════════════════════════════════════════════
# ALUMNOS
# ══════════════════════════════════════════════════════════════

@app.post("/alumno", summary="Registrar alumno")
def crear_alumno(a: Alumno):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                INSERT INTO alumnos (rfid_uid, name, account, materia, created_at)
                VALUES (:1, :2, :3, :4, :5)
            """, [a.rfid_uid, a.name, a.account, a.materia, a.created_at])
        conn.commit()
        log.info(f"Alumno insertado: {a.rfid_uid} - {a.name}")
        return {"ok": True, "msg": "Alumno registrado"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Alumno ya existe: {e}")
    finally:
        conn.close()


@app.get("/alumnos", summary="Listar alumnos")
def listar_alumnos(materia: Optional[str] = None):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            if materia:
                cur.execute("""
                    SELECT id, rfid_uid, name, account, materia, created_at
                    FROM alumnos
                    WHERE materia = :1
                    ORDER BY name
                """, [materia])
            else:
                cur.execute("""
                    SELECT id, rfid_uid, name, account, materia, created_at
                    FROM alumnos
                    ORDER BY name
                """)
            return rows_to_dicts(cur)
    finally:
        conn.close()


@app.get("/alumno/{alumno_id}", summary="Obtener alumno por ID")
def obtener_alumno(alumno_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT id, rfid_uid, name, account, materia, created_at
                FROM alumnos WHERE id = :1
            """, [alumno_id])
            row = fetch_one_dict(cur)
            if not row:
                raise HTTPException(status_code=404, detail="Alumno no encontrado")
            return row
    finally:
        conn.close()


@app.put("/alumno/{alumno_id}", summary="Actualizar alumno")
def actualizar_alumno(alumno_id: int, a: AlumnoUpdate):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT id, rfid_uid, name, account, materia, created_at
                FROM alumnos WHERE id = :1
            """, [alumno_id])
            old = fetch_one_dict(cur)
            if not old:
                raise HTTPException(status_code=404, detail="Alumno no encontrado")

            new_uid     = a.rfid_uid   if a.rfid_uid   is not None else old["rfid_uid"]
            new_name    = a.name       if a.name       is not None else old["name"]
            new_account = a.account    if a.account    is not None else old["account"]
            new_materia = a.materia    if a.materia    is not None else old["materia"]
            new_created = a.created_at if a.created_at is not None else old["created_at"]

            cur.execute("""
                UPDATE alumnos
                SET rfid_uid = :1, name = :2, account = :3, materia = :4, created_at = :5
                WHERE id = :6
            """, [new_uid, new_name, new_account, new_materia, new_created, alumno_id])

        conn.commit()
        return {"ok": True, "msg": "Alumno actualizado"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Alumno duplicado o inválido: {e}")
    finally:
        conn.close()


@app.delete("/alumno/{alumno_id}", summary="Eliminar alumno")
def eliminar_alumno(alumno_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("DELETE FROM alumnos WHERE id = :1", [alumno_id])
            deleted = cur.rowcount
        conn.commit()
        return {"ok": True, "deleted": deleted}
    finally:
        conn.close()


# ══════════════════════════════════════════════════════════════
# PROFESORES
# ══════════════════════════════════════════════════════════════

@app.post("/profesor", summary="Registrar profesor")
def crear_profesor(p: Profesor):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                INSERT INTO profesores (rfid_uid, name, account, created_at)
                VALUES (:1, :2, :3, :4)
            """, [p.rfid_uid, p.name, p.account, p.created_at])
        conn.commit()
        log.info(f"Profesor insertado: {p.rfid_uid} - {p.name}")
        return {"ok": True, "msg": "Profesor registrado"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Profesor ya existe: {e}")
    finally:
        conn.close()


@app.get("/profesores", summary="Listar profesores")
def listar_profesores():
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT rfid_uid, name, account, created_at
                FROM profesores ORDER BY name
            """)
            return rows_to_dicts(cur)
    finally:
        conn.close()


@app.get("/profesor/{rfid_uid}", summary="Obtener profesor por UID")
def obtener_profesor(rfid_uid: str):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT rfid_uid, name, account, created_at
                FROM profesores WHERE rfid_uid = :1
            """, [rfid_uid])
            row = fetch_one_dict(cur)
            if not row:
                raise HTTPException(status_code=404, detail="Profesor no encontrado")
            return row
    finally:
        conn.close()


@app.put("/profesor/{rfid_uid}", summary="Actualizar profesor")
def actualizar_profesor(rfid_uid: str, p: ProfesorUpdate):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT rfid_uid, name, account, created_at
                FROM profesores WHERE rfid_uid = :1
            """, [rfid_uid])
            old = fetch_one_dict(cur)
            if not old:
                raise HTTPException(status_code=404, detail="Profesor no encontrado")

            new_uid     = p.rfid_uid   if p.rfid_uid   is not None else old["rfid_uid"]
            new_name    = p.name       if p.name       is not None else old["name"]
            new_account = p.account    if p.account    is not None else old["account"]
            new_created = p.created_at if p.created_at is not None else old["created_at"]

            cur.execute("""
                UPDATE profesores
                SET rfid_uid = :1, name = :2, account = :3, created_at = :4
                WHERE rfid_uid = :5
            """, [new_uid, new_name, new_account, new_created, rfid_uid])

            if new_name != old["name"]:
                cur.execute("UPDATE materias SET profesor = :1 WHERE profesor = :2",
                            [new_name, old["name"]])
                cur.execute("UPDATE horarios SET profesor = :1 WHERE profesor = :2",
                            [new_name, old["name"]])

        conn.commit()
        return {"ok": True, "msg": "Profesor actualizado"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Profesor duplicado o inválido: {e}")
    finally:
        conn.close()


@app.delete("/profesor/{rfid_uid}", summary="Eliminar profesor")
def eliminar_profesor(rfid_uid: str, cascade: bool = Query(False)):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            if cascade:
                cur.execute(
                    "DELETE FROM horarios WHERE profesor = "
                    "(SELECT name FROM profesores WHERE rfid_uid = :1)", [rfid_uid])
                cur.execute("DELETE FROM profesor_materia WHERE rfid_uid = :1", [rfid_uid])
                cur.execute(
                    "DELETE FROM materias WHERE profesor = "
                    "(SELECT name FROM profesores WHERE rfid_uid = :1)", [rfid_uid])
            cur.execute("DELETE FROM profesores WHERE rfid_uid = :1", [rfid_uid])
            deleted = cur.rowcount
        conn.commit()
        return {"ok": True, "deleted": deleted}
    finally:
        conn.close()


# ══════════════════════════════════════════════════════════════
# MATERIAS
# ══════════════════════════════════════════════════════════════

@app.post("/materia", summary="Registrar materia")
def crear_materia(m: Materia):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            created = m.created_at if m.created_at else now_str()
            cur.execute("""
                INSERT INTO materias (materia, profesor, created_at)
                VALUES (:1, :2, :3)
            """, [m.materia, m.profesor, created])
        conn.commit()
        return {"ok": True, "msg": "Materia registrada"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Materia ya existe: {e}")
    finally:
        conn.close()


@app.get("/materias", summary="Listar materias")
def listar_materias():
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT materia, profesor, created_at FROM materias ORDER BY materia")
            return rows_to_dicts(cur)
    finally:
        conn.close()


@app.get("/materia/{materia}", summary="Obtener materia por nombre")
def obtener_materia(materia: str):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT materia, profesor, created_at FROM materias WHERE materia = :1",
                [materia])
            row = fetch_one_dict(cur)
            if not row:
                raise HTTPException(status_code=404, detail="Materia no encontrada")
            return row
    finally:
        conn.close()


@app.put("/materia/{materia}", summary="Actualizar materia")
def actualizar_materia(materia: str, m: MateriaUpdate):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT materia, profesor, created_at FROM materias WHERE materia = :1",
                [materia])
            old = fetch_one_dict(cur)
            if not old:
                raise HTTPException(status_code=404, detail="Materia no encontrada")

            new_mat     = m.materia    if m.materia    is not None else old["materia"]
            new_prof    = m.profesor   if m.profesor   is not None else old["profesor"]
            new_created = m.created_at if m.created_at is not None else old["created_at"]

            cur.execute("""
                UPDATE materias SET materia = :1, profesor = :2, created_at = :3
                WHERE materia = :4
            """, [new_mat, new_prof, new_created, materia])

        conn.commit()
        return {"ok": True, "msg": "Materia actualizada"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Materia duplicada o inválida: {e}")
    finally:
        conn.close()


@app.delete("/materia/{materia}", summary="Eliminar materia")
def eliminar_materia(materia: str, cascade: bool = Query(True)):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            if cascade:
                cur.execute("DELETE FROM horarios WHERE materia = :1", [materia])
                cur.execute("DELETE FROM profesor_materia WHERE materia = :1", [materia])
            cur.execute("DELETE FROM materias WHERE materia = :1", [materia])
            deleted = cur.rowcount
        conn.commit()
        return {"ok": True, "deleted": deleted}
    finally:
        conn.close()


# ══════════════════════════════════════════════════════════════
# PROFESOR_MATERIA
# ══════════════════════════════════════════════════════════════

@app.post("/profesor_materia", summary="Registrar relación profesor-materia")
def crear_profesor_materia(pm: ProfesorMateria):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            created = pm.created_at if pm.created_at else now_str()
            cur.execute("""
                INSERT INTO profesor_materia (rfid_uid, materia, created_at)
                VALUES (:1, :2, :3)
            """, [pm.rfid_uid, pm.materia, created])
        conn.commit()
        return {"ok": True, "msg": "Relación registrada"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Relación ya existe: {e}")
    finally:
        conn.close()


@app.get("/profesor_materia", summary="Listar relaciones profesor-materia")
def listar_profesor_materia():
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT id, rfid_uid, materia, created_at FROM profesor_materia ORDER BY materia")
            return rows_to_dicts(cur)
    finally:
        conn.close()


@app.get("/profesor_materia/{pm_id}", summary="Obtener relación por ID")
def obtener_profesor_materia(pm_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT id, rfid_uid, materia, created_at FROM profesor_materia WHERE id = :1",
                [pm_id])
            row = fetch_one_dict(cur)
            if not row:
                raise HTTPException(status_code=404, detail="Relación no encontrada")
            return row
    finally:
        conn.close()


@app.put("/profesor_materia/{pm_id}", summary="Actualizar relación")
def actualizar_profesor_materia(pm_id: int, pm: ProfesorMateriaUpdate):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT id, rfid_uid, materia, created_at FROM profesor_materia WHERE id = :1",
                [pm_id])
            old = fetch_one_dict(cur)
            if not old:
                raise HTTPException(status_code=404, detail="Relación no encontrada")

            new_uid     = pm.rfid_uid   if pm.rfid_uid   is not None else old["rfid_uid"]
            new_mat     = pm.materia    if pm.materia    is not None else old["materia"]
            new_created = pm.created_at if pm.created_at is not None else old["created_at"]

            cur.execute("""
                UPDATE profesor_materia SET rfid_uid = :1, materia = :2, created_at = :3
                WHERE id = :4
            """, [new_uid, new_mat, new_created, pm_id])

        conn.commit()
        return {"ok": True, "msg": "Relación actualizada"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Relación duplicada o inválida: {e}")
    finally:
        conn.close()


@app.delete("/profesor_materia/{pm_id}", summary="Eliminar relación")
def eliminar_profesor_materia(pm_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("DELETE FROM profesor_materia WHERE id = :1", [pm_id])
            deleted = cur.rowcount
        conn.commit()
        return {"ok": True, "deleted": deleted}
    finally:
        conn.close()


# ══════════════════════════════════════════════════════════════
# HORARIOS
# ══════════════════════════════════════════════════════════════

@app.post("/horario", summary="Registrar horario")
def crear_horario(h: Horario):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            created = h.created_at if h.created_at else now_str()
            cur.execute("""
                INSERT INTO horarios (materia, profesor, dia, hora_inicio, hora_fin, created_at)
                VALUES (:1, :2, :3, :4, :5, :6)
            """, [h.materia, h.profesor, h.dia, h.hora_inicio, h.hora_fin, created])
        conn.commit()
        return {"ok": True, "msg": "Horario registrado"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Horario duplicado o slot ocupado: {e}")
    finally:
        conn.close()


@app.get("/horarios", summary="Listar horarios")
def listar_horarios(materia: Optional[str] = None):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            if materia:
                cur.execute("""
                    SELECT id, materia, profesor, dia, hora_inicio, hora_fin, created_at
                    FROM horarios WHERE materia = :1 ORDER BY dia, hora_inicio
                """, [materia])
            else:
                cur.execute("""
                    SELECT id, materia, profesor, dia, hora_inicio, hora_fin, created_at
                    FROM horarios ORDER BY materia, dia, hora_inicio
                """)
            return rows_to_dicts(cur)
    finally:
        conn.close()


@app.get("/horario/{horario_id}", summary="Obtener horario por ID")
def obtener_horario(horario_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT id, materia, profesor, dia, hora_inicio, hora_fin, created_at
                FROM horarios WHERE id = :1
            """, [horario_id])
            row = fetch_one_dict(cur)
            if not row:
                raise HTTPException(status_code=404, detail="Horario no encontrado")
            return row
    finally:
        conn.close()


@app.put("/horario/{horario_id}", summary="Actualizar horario")
def actualizar_horario(horario_id: int, h: HorarioUpdate):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT id, materia, profesor, dia, hora_inicio, hora_fin, created_at
                FROM horarios WHERE id = :1
            """, [horario_id])
            old = fetch_one_dict(cur)
            if not old:
                raise HTTPException(status_code=404, detail="Horario no encontrado")

            new_materia  = h.materia     if h.materia     is not None else old["materia"]
            new_profesor = h.profesor    if h.profesor    is not None else old["profesor"]
            new_dia      = h.dia         if h.dia         is not None else old["dia"]
            new_inicio   = h.hora_inicio if h.hora_inicio is not None else old["hora_inicio"]
            new_fin      = h.hora_fin    if h.hora_fin    is not None else old["hora_fin"]
            new_created  = h.created_at  if h.created_at  is not None else old["created_at"]

            cur.execute("""
                UPDATE horarios
                SET materia = :1, profesor = :2, dia = :3,
                    hora_inicio = :4, hora_fin = :5, created_at = :6
                WHERE id = :7
            """, [new_materia, new_profesor, new_dia, new_inicio, new_fin, new_created, horario_id])

        conn.commit()
        return {"ok": True, "msg": "Horario actualizado"}
    except oracledb.IntegrityError as e:
        raise HTTPException(status_code=409, detail=f"Horario duplicado o inválido: {e}")
    finally:
        conn.close()


@app.delete("/horario/{horario_id}", summary="Eliminar horario")
def eliminar_horario(horario_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("DELETE FROM horarios WHERE id = :1", [horario_id])
            deleted = cur.rowcount
        conn.commit()
        return {"ok": True, "deleted": deleted}
    finally:
        conn.close()


# ══════════════════════════════════════════════════════════════
# ASISTENCIAS
# Se usa alias de tabla 'as_' para que Oracle no confunda
# 'timestamp' y 'mode' con palabras reservadas (ORA-00936).
# ══════════════════════════════════════════════════════════════

@app.post("/asistencia", summary="Registrar asistencia (idempotente)")
def registrar_asistencia(a: Asistencia):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            inserted = _insert_asistencia_idempotent(cur, a)
        conn.commit()
        if inserted:
            log.info(f"Asistencia nueva: {a.rfid_uid} - {a.name} [{a.mode}]")
            return {"ok": True, "msg": "Asistencia registrada", "new": True}
        else:
            log.debug(f"Asistencia ya existía (skip): {a.rfid_uid} [{a.mode}] {a.timestamp}")
            return {"ok": True, "msg": "Asistencia ya existía (sin duplicado)", "new": False}
    finally:
        conn.close()


@app.get("/asistencias", summary="Listar asistencias")
def listar_asistencias(rfid_uid: Optional[str] = None, materia: Optional[str] = None):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            query = """
                SELECT as_.id, as_.fecha_hora, as_.rfid_uid, as_.name,
                       as_.account, as_.materia, as_.tipo
                FROM asistencias as_
                WHERE 1=1
            """
            params = []

            if rfid_uid is not None:
                query += " AND as_.rfid_uid = :1"
                params.append(rfid_uid)

            if materia is not None:
                query += f" AND as_.materia = :{len(params) + 1}"
                params.append(materia)

            query += ' ORDER BY as_.fecha_hora DESC'
            cur.execute(query, params)
            return rows_to_dicts(cur)
    finally:
        conn.close()


@app.get("/asistencia/{asistencia_id}", summary="Obtener asistencia por ID")
def obtener_asistencia(asistencia_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT as_.id, as_.fecha_hora, as_.rfid_uid, as_.name,
                       as_.account, as_.materia, as_.tipo
                FROM asistencias as_ WHERE as_.id = :1
            """, [asistencia_id])
            row = fetch_one_dict(cur)
            if not row:
                raise HTTPException(status_code=404, detail="Asistencia no encontrada")
            return row
    finally:
        conn.close()


@app.put("/asistencia/{asistencia_id}", summary="Actualizar asistencia")
def actualizar_asistencia(asistencia_id: int, a: AsistenciaUpdate):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT as_.id, as_.fecha_hora, as_.rfid_uid, as_.name,
                       as_.account, as_.materia, as_.tipo
                FROM asistencias as_ WHERE as_.id = :1
            """, [asistencia_id])
            old = fetch_one_dict(cur)
            if not old:
                raise HTTPException(status_code=404, detail="Asistencia no encontrada")

            new_ts      = a.timestamp if a.timestamp is not None else old["fecha_hora"]
            new_uid     = a.rfid_uid  if a.rfid_uid  is not None else old["rfid_uid"]
            new_name    = a.name      if a.name      is not None else old["name"]
            new_account = a.account   if a.account   is not None else old["account"]
            new_materia = a.materia   if a.materia   is not None else old["materia"]
            new_mode    = a.mode      if a.mode      is not None else old["tipo"]

            cur.execute("""
                UPDATE asistencias as_
                SET as_.fecha_hora = :1, as_.rfid_uid = :2, as_.name = :3,
                    as_.account = :4, as_.materia = :5, as_.tipo = :6
                WHERE as_.id = :7
            """, [new_ts, new_uid, new_name, new_account, new_materia, new_mode, asistencia_id])

        conn.commit()
        return {"ok": True, "msg": "Asistencia actualizada"}
    finally:
        conn.close()


@app.delete("/asistencia/{asistencia_id}", summary="Eliminar asistencia")
def eliminar_asistencia(asistencia_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("DELETE FROM asistencias WHERE id = :1", [asistencia_id])
            deleted = cur.rowcount
        conn.commit()
        return {"ok": True, "deleted": deleted}
    finally:
        conn.close()


# ══════════════════════════════════════════════════════════════
# ACCESOS DENEGADOS
# Alias 'ad_' para timestamp. TO_CHAR para comparar CLOB note.
# ══════════════════════════════════════════════════════════════

@app.post("/acceso_denegado", summary="Registrar acceso denegado (idempotente)")
def registrar_acceso_denegado(a: AccesoDenegado):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            inserted = _insert_denegado_idempotent(cur, a)
        conn.commit()
        if inserted:
            log.warning(f"Acceso denegado nuevo: {a.rfid_uid}")
            return {"ok": True, "msg": "Acceso denegado registrado", "new": True}
        else:
            log.debug(f"Acceso denegado ya existía (skip): {a.rfid_uid} {a.timestamp}")
            return {"ok": True, "msg": "Acceso denegado ya existía (sin duplicado)", "new": False}
    finally:
        conn.close()


@app.get("/accesos_denegados", summary="Listar accesos denegados")
def listar_accesos_denegados():
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT ad_.id, ad_.timestamp, ad_.rfid_uid, TO_CHAR(ad_.note) AS note
                FROM accesos_denegados ad_
                ORDER BY ad_.timestamp DESC
            """)
            return rows_to_dicts(cur)
    finally:
        conn.close()


@app.get("/acceso_denegado/{denegado_id}", summary="Obtener acceso denegado por ID")
def obtener_acceso_denegado(denegado_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT ad_.id, ad_.timestamp, ad_.rfid_uid, TO_CHAR(ad_.note) AS note
                FROM accesos_denegados ad_ WHERE ad_.id = :1
            """, [denegado_id])
            row = fetch_one_dict(cur)
            if not row:
                raise HTTPException(status_code=404, detail="Registro no encontrado")
            return row
    finally:
        conn.close()


@app.put("/acceso_denegado/{denegado_id}", summary="Actualizar acceso denegado")
def actualizar_acceso_denegado(denegado_id: int, a: AccesoDenegadoUpdate):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT ad_.id, ad_.timestamp, ad_.rfid_uid, TO_CHAR(ad_.note) AS note
                FROM accesos_denegados ad_ WHERE ad_.id = :1
            """, [denegado_id])
            old = fetch_one_dict(cur)
            if not old:
                raise HTTPException(status_code=404, detail="Registro no encontrado")

            new_ts   = a.timestamp if a.timestamp is not None else old["timestamp"]
            new_uid  = a.rfid_uid  if a.rfid_uid  is not None else old["rfid_uid"]
            new_note = a.note      if a.note      is not None else old["note"]

            cur.execute("""
                UPDATE accesos_denegados ad_
                SET ad_.timestamp = :1, ad_.rfid_uid = :2, ad_.note = :3
                WHERE ad_.id = :4
            """, [new_ts, new_uid, new_note, denegado_id])

        conn.commit()
        return {"ok": True, "msg": "Acceso denegado actualizado"}
    finally:
        conn.close()


@app.delete("/acceso_denegado/{denegado_id}", summary="Eliminar acceso denegado")
def eliminar_acceso_denegado(denegado_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("DELETE FROM accesos_denegados WHERE id = :1", [denegado_id])
            deleted = cur.rowcount
        conn.commit()
        return {"ok": True, "deleted": deleted}
    finally:
        conn.close()


# ══════════════════════════════════════════════════════════════
# NOTIFICACIONES
# Alias 'nt_' para timestamp. TO_CHAR para comparar/leer CLOB note.
# ══════════════════════════════════════════════════════════════

@app.post("/notificacion", summary="Registrar notificación (idempotente)")
def registrar_notificacion(n: Notificacion):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            inserted = _insert_notificacion_idempotent(cur, n)
        conn.commit()
        if inserted:
            log.info(f"Notificación nueva: {n.rfid_uid}")
            return {"ok": True, "msg": "Notificación registrada", "new": True}
        else:
            log.debug(f"Notificación ya existía (skip): {n.rfid_uid} {n.timestamp}")
            return {"ok": True, "msg": "Notificación ya existía (sin duplicado)", "new": False}
    finally:
        conn.close()


@app.get("/notificaciones", summary="Listar notificaciones")
def listar_notificaciones(solo_no_leidas: bool = False):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            if solo_no_leidas:
                cur.execute("""
                    SELECT nt_.id, nt_.timestamp, nt_.rfid_uid, nt_.name,
                           nt_.account, TO_CHAR(nt_.note) AS note, nt_.leida
                    FROM notificaciones nt_
                    WHERE nt_.leida = 0
                    ORDER BY nt_.timestamp DESC
                """)
            else:
                cur.execute("""
                    SELECT nt_.id, nt_.timestamp, nt_.rfid_uid, nt_.name,
                           nt_.account, TO_CHAR(nt_.note) AS note, nt_.leida
                    FROM notificaciones nt_
                    ORDER BY nt_.timestamp DESC
                """)
            return rows_to_dicts(cur)
    finally:
        conn.close()


@app.get("/notificacion/{notif_id}", summary="Obtener notificación por ID")
def obtener_notificacion(notif_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT nt_.id, nt_.timestamp, nt_.rfid_uid, nt_.name,
                       nt_.account, TO_CHAR(nt_.note) AS note, nt_.leida
                FROM notificaciones nt_ WHERE nt_.id = :1
            """, [notif_id])
            row = fetch_one_dict(cur)
            if not row:
                raise HTTPException(status_code=404, detail="Notificación no encontrada")
            return row
    finally:
        conn.close()


@app.put("/notificacion/{notif_id}", summary="Actualizar notificación")
def actualizar_notificacion(notif_id: int, n: NotificacionUpdate):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT nt_.id, nt_.timestamp, nt_.rfid_uid, nt_.name,
                       nt_.account, TO_CHAR(nt_.note) AS note, nt_.leida
                FROM notificaciones nt_ WHERE nt_.id = :1
            """, [notif_id])
            old = fetch_one_dict(cur)
            if not old:
                raise HTTPException(status_code=404, detail="Notificación no encontrada")

            new_ts      = n.timestamp if n.timestamp is not None else old["timestamp"]
            new_uid     = n.rfid_uid  if n.rfid_uid  is not None else old["rfid_uid"]
            new_name    = n.name      if n.name      is not None else old["name"]
            new_account = n.account   if n.account   is not None else old["account"]
            new_note    = n.note      if n.note      is not None else old["note"]
            new_leida   = n.leida     if n.leida     is not None else old["leida"]

            cur.execute("""
                UPDATE notificaciones nt_
                SET nt_.timestamp = :1, nt_.rfid_uid = :2, nt_.name = :3,
                    nt_.account = :4, nt_.note = :5, nt_.leida = :6
                WHERE nt_.id = :7
            """, [new_ts, new_uid, new_name, new_account, new_note, new_leida, notif_id])

        conn.commit()
        return {"ok": True, "msg": "Notificación actualizada"}
    finally:
        conn.close()


@app.put("/notificacion/{notif_id}/leida", summary="Marcar notificación como leída")
def marcar_notificacion_leida(notif_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("UPDATE notificaciones SET leida = 1 WHERE id = :1", [notif_id])
        conn.commit()
        return {"ok": True}
    finally:
        conn.close()


@app.delete("/notificacion/{notif_id}", summary="Eliminar notificación")
def eliminar_notificacion(notif_id: int):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute("DELETE FROM notificaciones WHERE id = :1", [notif_id])
            deleted = cur.rowcount
        conn.commit()
        return {"ok": True, "deleted": deleted}
    finally:
        conn.close()


# ══════════════════════════════════════════════════════════════
# SINCRONIZACIÓN EN LOTE (idempotente)
# ══════════════════════════════════════════════════════════════

@app.post("/sync", summary="Sincronización en lote desde el ESP32 (idempotente)")
def sync_batch(payload: SyncPayload):
    """
    El ESP32 acumula registros en SPIFFS y los envía aquí
    cuando el servidor está disponible (en arranque y cada ~15 s).
    Si un registro ya existe en Oracle, se omite silenciosamente.
    Devuelve cuántos registros nuevos se insertaron y cuántos ya existían.
    """
    conn = get_connection()
    resultados = {
        "asistencias_nuevas": 0,
        "asistencias_existentes": 0,
        "accesos_denegados_nuevos": 0,
        "accesos_denegados_existentes": 0,
        "notificaciones_nuevas": 0,
        "notificaciones_existentes": 0,
        "errores": []
    }

    try:
        with conn.cursor() as cur:

            for a in payload.asistencias:
                try:
                    if _insert_asistencia_idempotent(cur, a):
                        resultados["asistencias_nuevas"] += 1
                    else:
                        resultados["asistencias_existentes"] += 1
                except oracledb.DatabaseError as e:
                    resultados["errores"].append(f"Asistencia {a.rfid_uid}@{a.timestamp}: {e}")

            for d in payload.accesos_denegados:
                try:
                    if _insert_denegado_idempotent(cur, d):
                        resultados["accesos_denegados_nuevos"] += 1
                    else:
                        resultados["accesos_denegados_existentes"] += 1
                except oracledb.DatabaseError as e:
                    resultados["errores"].append(f"Denegado {d.rfid_uid}@{d.timestamp}: {e}")

            for n in payload.notificaciones:
                try:
                    if _insert_notificacion_idempotent(cur, n):
                        resultados["notificaciones_nuevas"] += 1
                    else:
                        resultados["notificaciones_existentes"] += 1
                except oracledb.DatabaseError as e:
                    resultados["errores"].append(f"Notif {n.rfid_uid}@{n.timestamp}: {e}")

        conn.commit()
        log.info(
            f"Sync lote → asistencias: +{resultados['asistencias_nuevas']} "
            f"(ya existían: {resultados['asistencias_existentes']}), "
            f"denegados: +{resultados['accesos_denegados_nuevos']} "
            f"(ya existían: {resultados['accesos_denegados_existentes']}), "
            f"notifs: +{resultados['notificaciones_nuevas']} "
            f"(ya existían: {resultados['notificaciones_existentes']})"
        )
        return {"ok": True, "resultados": resultados}

    except Exception as e:
        conn.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        conn.close()
    

# ══════════════════════════════════════════════════════════════
# ARRANQUE LOCAL
# ══════════════════════════════════════════════════════════════

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)