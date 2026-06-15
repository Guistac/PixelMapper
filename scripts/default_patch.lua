-- Default PixelMapper Lua Script
-- update(canvas, time) is called once per frame

function update(canvas, time)
    -- clear screen with black
    canvas:clear(0, 0, 0, 0)
    
    local w = canvas:width()
    local h = canvas:height()
    
    -- Draw a moving circle sweep
    local cx = w * 0.5 + math.cos(time * 2.0) * (w * 0.3)
    local cy = h * 0.5 + math.sin(time * 3.0) * (h * 0.3)
    local radius = 15.0 + math.sin(time * 5.0) * 5.0
    
    -- Draw filled circle with color shifting over time
    local r = math.floor((math.sin(time) * 0.5 + 0.5) * 255)
    local g = math.floor((math.sin(time + 2.0) * 0.5 + 0.5) * 255)
    local b = math.floor((math.sin(time + 4.0) * 0.5 + 0.5) * 255)
    
    canvas:draw_circle(math.floor(cx), math.floor(cy), math.floor(radius), r, g, b, 255, true)
    
    -- Draw some noise lines / points
    for i = 0, 10 do
        local nx = math.floor(w * 0.5 + (canvas:noise(i * 10.0, time * 0.5, 0.0)) * (w * 0.4))       
        local ny = math.floor(h * 0.5 + (canvas:noise(0.0, i * 10.0, time * 0.5)) * (h * 0.4))
        canvas:set_pixel(nx, ny, 255, 255, 255, 255)
    end
end
