# -*- coding: utf-8 -*-
"""Arma docs/cubicaje-motor-optimizacion.html a partir de deck.json y slides/*.html.

Uso:  python construir_html.py
Genera un solo archivo HTML sin dependencias externas (fuentes incrustadas en
fuentes.css) con navegación por teclado, notas del orador e impresión a PDF.
"""
import io
import json
import os
import re

AQUI = os.path.dirname(os.path.abspath(__file__))
SALIDA = os.path.join(os.path.dirname(AQUI), "cubicaje-motor-optimizacion.html")


def leer(ruta):
    with io.open(ruta, encoding="utf-8") as f:
        return f.read()


deck = json.loads(leer(os.path.join(AQUI, "deck.json")))
fuentes = leer(os.path.join(AQUI, "fuentes.css"))

secciones = []
for sid in deck["order"]:
    html = leer(os.path.join(AQUI, "slides", sid + ".html")).strip()
    html = re.sub(r"<section ", '<section class="slide" ', html, count=1)
    secciones.append(html)

CSS = """
*{box-sizing:border-box;margin:0}
html,body{height:100%;background:#0F1114;color:#F5F4F0;font-family:'Barlow','Segoe UI',sans-serif}
body{overflow:hidden}
.frame{position:fixed;inset:0;display:grid;place-items:center}
.stage{width:1920px;height:1080px;position:relative;transform-origin:center center;box-shadow:0 20px 60px rgba(0,0,0,.5)}
section.slide{position:absolute;inset:0;width:1920px;height:1080px;overflow:hidden}
section.slide:not(.activa){display:none!important}
section.slide div{min-width:0}
section.slide div:not([style*="display"]){display:flex;flex-direction:column}
section.slide h1{font-size:96px;font-weight:600;line-height:1.1}
section.slide h2{font-size:64px;font-weight:600;line-height:1.15}
section.slide h3{font-size:44px;font-weight:600;line-height:1.2}
section.slide p{font-size:32px;font-weight:400;line-height:1.4}
section.slide ul,section.slide ol{padding-left:1.2em}
section.slide li{line-height:inherit}
section.slide b{font-weight:600}
section.slide table{border-collapse:collapse;width:100%;table-layout:fixed}
section.slide td,section.slide th{padding:.35em .6em;border:1px solid rgba(127,127,127,.35);vertical-align:top;text-align:left;font-weight:inherit;line-height:1.3;word-wrap:break-word}
section.slide th{font-weight:600}
section.slide [style*="width:"]{flex-shrink:0}
section.slide aside{display:none}
.hud{position:fixed;left:0;right:0;bottom:0;display:flex;justify-content:space-between;align-items:center;padding:10px 16px;font:14px 'JetBrains Mono','Courier New',monospace;color:#B9B5AA;pointer-events:none}
.hud span{pointer-events:auto}
.hud .ayuda{opacity:.7}
.notas{position:fixed;right:0;top:0;bottom:0;width:380px;background:#171A1F;color:#F5F4F0;border-left:1px solid #464B54;padding:24px;font-size:16px;line-height:1.5;display:none;overflow:auto}
.notas h4{font:600 13px 'JetBrains Mono',monospace;color:#F2B01E;letter-spacing:2px;text-transform:uppercase;margin-bottom:12px}
body.con-notas .notas{display:block}
.zona{position:fixed;top:0;bottom:48px;width:30%;cursor:pointer}
.zona.izq{left:0}.zona.der{right:0}
@media print{
  @page{size:1920px 1080px;margin:0}
  html,body{background:#fff;overflow:visible;height:auto}
  .frame{position:static;display:block}
  .stage{transform:none!important;box-shadow:none;width:auto;height:auto}
  section.slide{position:relative;display:flex!important;break-after:page;page-break-after:always}
  .hud,.notas,.zona{display:none!important}
}
"""

JS = """
(function(){
  var slides=[].slice.call(document.querySelectorAll('section.slide'));
  var stage=document.querySelector('.stage');
  var cont=document.getElementById('contador');
  var notas=document.getElementById('notas-texto');
  var i=0;
  function escala(){
    var s=Math.min(window.innerWidth/1920,(window.innerHeight-48)/1080);
    stage.style.transform='scale('+s+')';
  }
  function ir(n){
    n=Math.max(0,Math.min(slides.length-1,n));
    slides.forEach(function(s,k){s.classList.toggle('activa',k===n);});
    i=n;
    cont.textContent=(n+1)+' / '+slides.length;
    var a=slides[n].querySelector('aside');
    notas.textContent=a?a.textContent:'(sin notas)';
    if(location.hash!=='#'+(n+1)) history.replaceState(null,'','#'+(n+1));
  }
  function desdeHash(){
    var h=parseInt(location.hash.replace('#',''),10);
    ir(isNaN(h)?0:h-1);
  }
  document.addEventListener('keydown',function(e){
    if(e.target&&/INPUT|TEXTAREA/.test(e.target.tagName))return;
    switch(e.key){
      case 'ArrowRight':case 'ArrowDown':case 'PageDown':case ' ':ir(i+1);e.preventDefault();break;
      case 'ArrowLeft':case 'ArrowUp':case 'PageUp':case 'Backspace':ir(i-1);e.preventDefault();break;
      case 'Home':ir(0);break;
      case 'End':ir(slides.length-1);break;
      case 'n':case 'N':document.body.classList.toggle('con-notas');escala();break;
      case 'f':case 'F':if(document.fullscreenElement){document.exitFullscreen();}else{document.documentElement.requestFullscreen();}break;
    }
  });
  document.querySelector('.zona.izq').addEventListener('click',function(){ir(i-1);});
  document.querySelector('.zona.der').addEventListener('click',function(){ir(i+1);});
  window.addEventListener('resize',escala);
  window.addEventListener('hashchange',desdeHash);
  window.addEventListener('beforeprint',function(){slides.forEach(function(s){s.classList.add('activa');});});
  window.addEventListener('afterprint',function(){ir(i);});
  /* Ajuste automático: si el contenido de una lámina desborda el lienzo,
     se reducen todas sus fuentes un 5 % por paso (como hace el visor original). */
  function desborda(sec){
    var r=sec.getBoundingClientRect(), s=r.height/1080, lim=r.top+975*s;
    var els=sec.querySelectorAll('*');
    for(var k=0;k<els.length;k++){
      var el=els[k];
      if(el.tagName==='ASIDE')continue;
      var fijo=false,pa=el;
      while(pa&&pa!==sec){if(getComputedStyle(pa).position==='absolute'){fijo=true;break;}pa=pa.parentElement;}
      if(fijo)continue;
      if(el.getBoundingClientRect().bottom>lim)return true;
    }
    return false;
  }
  function encoger(sec,f){
    var els=sec.querySelectorAll('[style*="font-size"]');
    for(var k=0;k<els.length;k++){
      var v=parseFloat(els[k].style.fontSize);
      if(v)els[k].style.fontSize=(v*f).toFixed(2)+'px';
    }
  }
  function ajustarTodo(){
    var prev=i;
    slides.forEach(function(sec,k){
      slides.forEach(function(s2,j){s2.classList.toggle('activa',j===k);});
      var pasos=0;
      while(pasos<8&&desborda(sec)){encoger(sec,0.95);pasos++;}
    });
    ir(prev);
  }
  escala();desdeHash();
  if(document.fonts&&document.fonts.ready){document.fonts.ready.then(ajustarTodo);}else{setTimeout(ajustarTodo,300);}
})();
"""

titulo = deck.get("title", "Presentación")
partes = [
    "<!doctype html>",
    '<html lang="es"><head><meta charset="utf-8">',
    '<meta name="viewport" content="width=device-width,initial-scale=1">',
    "<title>" + titulo + "</title>",
    "<style>" + fuentes + "</style>",
    "<style>" + CSS + "</style>",
    "</head><body>",
    '<div class="frame"><div class="stage">',
    "\n".join(secciones),
    "</div></div>",
    '<div class="zona izq" title="Anterior"></div><div class="zona der" title="Siguiente"></div>',
    '<aside class="notas"><h4>Notas del orador</h4><div id="notas-texto"></div></aside>',
    '<div class="hud"><span class="ayuda">← → avanzar · N notas · F pantalla completa · Ctrl+P imprime una lámina por página</span><span id="contador"></span></div>',
    "<script>" + JS + "</script>",
    "</body></html>",
]

with io.open(SALIDA, "w", encoding="utf-8") as f:
    f.write("\n".join(partes))
print("Escrito", SALIDA, os.path.getsize(SALIDA), "bytes,", len(secciones), "láminas")
