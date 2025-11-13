#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "capture.h"
#include "globals.h"
#include "web_common.h"

// Captura página (GET)
void handleCapturePage() {
  captureMode = true;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  String html = htmlHeader("Capturar Tarjeta (escuchando)");
  html += "<div class='card'><h2>Captura automática</h2>";
  html += "<p class='small'>Acerca la tarjeta ahora; si ya existe en otra materia se autocompletan Nombre y Cuenta. Selecciona Materia (obligatorio). Si la tarjeta no existe, ingresa Nombre y Cuenta (7 dígitos) y selecciona Materia.</p>";
  html += "<form id='capForm' method='POST' action='/capture_confirm'>";
  html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input id='name' name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7'><br>";

  auto courses = loadCourses();
  html += "Materia (seleccionar):<br><select id='materia' name='materia'>";
  html += "<option value=''>-- Seleccionar materia --</option>";
  for (auto &c : courses) html += "<option value='" + c.materia + "'>" + c.materia + " (" + c.profesor + ")</option>";
  html += "</select><br>";
  html += "<div id='newMatDiv' style='display:none;margin-top:6px'>Nueva materia:<br><input id='newMateria' name='newMateria' disabled></div><br>";
  html += "<a class='btn btn-red' href='/' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</form>";

  html += "<script>\n"
          "let saved=false;\n"
          "function isAccountValid(s){ return /^[0-9]{7}$/.test(s); }\n"
          "function tryAutoSubmit(){ if(saved) return; const uid=document.getElementById('uid').value.trim(); const name=document.getElementById('name').value.trim(); const account=document.getElementById('account').value.trim(); let materia=document.getElementById('materia').value.trim(); if(materia=='__new__'){ materia=document.getElementById('newMateria').value.trim(); }\n"
          " if(uid.length==0) return; if(name.length>0 && isAccountValid(account) && materia.length>0){ saved=true; let form=new FormData(); form.append('uid',uid); form.append('name',name); form.append('account',account); form.append('materia',materia); fetch('/capture_confirm',{method:'POST',body:form}).then(r=>{ window.location='/'; }).catch(e=>{ alert('Error al guardar: '+e); saved=false; }); } }\n"
          "function poll(){ fetch('/capture_poll').then(r=>r.json()).then(j=>{ if(j.status=='waiting'){ setTimeout(poll,700); } else if(j.status=='found'){ document.getElementById('uid').value=j.uid; if(j.name) document.getElementById('name').value=j.name; if(j.account) document.getElementById('account').value=j.account; tryAutoSubmit(); setTimeout(poll,700); } else { setTimeout(poll,700); } }).catch(e=>setTimeout(poll,1200)); }\n"
          "document.addEventListener('input', function(e){ setTimeout(tryAutoSubmit,300); }); var sel=document.getElementById('materia'); if(sel) sel.addEventListener('change', function(){ if(this.value=='__new__'){ document.getElementById('newMatDiv').style.display='block'; } else { document.getElementById('newMatDiv').style.display='none'; } setTimeout(tryAutoSubmit,200); }); poll();\n"
          "window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e) {} });\n"
          "</script>";

  html += "</div>" + htmlFooter();
  server.send(200,"text/html",html);
}

void handleCapturePoll() {
  if (captureUID.length() == 0) {
    server.send(200,"application/json","{\"status\":\"waiting\"}");
    return;
  }
  String j = "{\"status\":\"found\",\"uid\":\"" + captureUID + "\"";
  if (captureName.length()) j += ",\"name\":\"" + captureName + "\"";
  if (captureAccount.length()) j += ",\"account\":\"" + captureAccount + "\"";
  j += "}";
  server.send(200,"application/json", j);
}

void handleCaptureConfirm() {
  if (!server.hasArg("uid") || !server.hasArg("name") || !server.hasArg("account") || !server.hasArg("materia")) {
    server.send(400,"text/plain","Faltan parametros"); return;
  }
  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.arg("materia"); materia.trim();

  if (uid.length()==0) { server.send(400,"text/plain","UID vacio"); return; }
  bool ok=true;
  if (account.length()!=7) ok=false;
  for (size_t i=0;i<account.length();i++) if (!isDigit(account[i])) ok=false;
  if (!ok) { server.send(400,"text/plain","Cuenta invalida"); return; }

  if (!courseExists(materia)) {
    server.send(400,"text/plain","La materia especificada no existe. Registrela primero en Materias.");
    return;
  }

  if (existsUserUidMateria(uid,materia)) {
    captureMode = false;
    server.sendHeader("Location","/capture?msg=duplicado");
    server.send(303,"text/plain","Duplicado");
    return;
  }
  String created = nowISO();
  String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"" + created + "\"";
  appendLineToFile(USERS_FILE, line);
  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura\"";
  appendLineToFile(ATT_FILE, rec);
  captureMode = false; captureUID=""; captureName=""; captureAccount=""; captureDetectedAt=0;
  server.sendHeader("Location","/");
  server.send(303,"text/plain","Usuario registrado");
}

void handleCaptureStopGET() {
  captureMode = false; captureUID=""; captureName=""; captureAccount=""; captureDetectedAt=0;
  server.sendHeader("Location","/"); server.send(303,"text/plain","stopped");
}
