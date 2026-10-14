-- layouts/form.lua : behaviour for layout_form.xml.
-- The engine owns the loop; this script only reacts to the four hooks and
-- talks to the widgets through the ogui API.

local target = 0   -- progress the send button queues for ogui_on_update

function ogui_on_load()
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
    target = 1
    ogui.set("f_status", "text", "Enviando " .. nome .. " ...")
    ogui.set("f_send", "visible", false)
  end
end

function ogui_on_update(t, dt)
  local p = ogui.get("f_prog", "value")
  if p < target then
    p = math.min(target, p + dt * 0.8)
    ogui.set("f_prog", "value", p)
    if p >= target then
      ogui.set("f_status", "text", "Cadastro concluido!")
      ogui.toast("Cadastro concluido", "info")
    end
  end
end
