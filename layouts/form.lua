-- layouts/form.lua : behaviour for layout_form.xml.
-- The engine owns the loop; this script only reacts to the hooks and
-- talks to the widgets through the ogui API.

local sent = false

function ogui_on_load()
  sent = false
  ogui.set("f_status", "text", "Preencha o formulario.")
  ogui.set("f_prog", "value", 0)
end

function ogui_on_click(key)
  if key ~= "f_send" then return end

  local nome   = ogui.get("f_name", "text") or ""
  local email  = ogui.get("f_email", "text") or ""
  local termos = ogui.get("f_terms", "checked")

  if #nome < 2 then
    ogui.set("f_status", "text", "Nome muito curto.")
    ogui.toast("Informe seu nome", "error")
  elseif email == "" then
    ogui.set("f_status", "text", "Falta o e-mail.")
    ogui.toast("Falta o e-mail", "warn")
  elseif not termos then
    ogui.set("f_status", "text", "Aceite os termos para enviar.")
    ogui.toast("Aceite os termos", "warn")
  else
    sent = true
    ogui.set("f_status", "text", "Enviando " .. nome .. " ...")
    ogui.set("f_send", "visible", false)
    ogui.tween("f_prog", "value", 1, 1.2, "out")
  end
end

function ogui_on_update()
  if sent and ogui.get("f_prog", "value") >= 1 then
    sent = false
    ogui.set("f_status", "text", "Cadastro concluido!")
    ogui.toast("Cadastro concluido", "info")
  end
end
