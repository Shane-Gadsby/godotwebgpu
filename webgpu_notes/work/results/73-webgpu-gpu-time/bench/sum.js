for (const f of process.argv.slice(2)) { const j=require("./"+f+".json"); console.log("##", f, (j.adapter||"").slice(0,90), "| errors", j.errors.length, "| completed", j.completed);
for (const e of j.errors.slice(0,3)) console.log("  ERR", e.slice(0,200));
for (const l of j.lines.filter(l=>l.kind=="sweep")) console.log(" ", l.scale, "frame", l.frame_med.toFixed(1), "p90", l.frame_p90.toFixed(1), "hook", l.gpu_mean.toFixed(2), "engine", l.engine_gpu_mean.toFixed(2), l.gpu_mean>0?"ratio "+(l.engine_gpu_mean/l.gpu_mean).toFixed(3):""); }
