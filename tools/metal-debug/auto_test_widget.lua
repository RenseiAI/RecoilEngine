-- Metal Debug Auto-Test Widget
-- Placed into LuaUI/Widgets/ by run-test.sh
-- Takes screenshots at key frames and exits after target frame count.
--
-- The TARGET_FRAMES placeholder is replaced by run-test.sh.

function widget:GetInfo()
    return {
        name    = "Metal Debug Auto-Test",
        desc    = "Screenshots + auto-quit for automated Metal testing",
        author  = "metal-debug",
        date    = "2026",
        license = "GPL",
        layer   = 0,
        enabled = true,
    }
end

local TARGET_FRAMES = %%TARGET_FRAMES%%
local screenshotAt = {1, 10, 30, 60, 120, 180, 300, 450, 600, 750, 900}
local taken = {}
local drawFrames = 0
local startTimer = nil
local sentForceStart = false

function widget:Initialize()
    startTimer = Spring.GetTimer()
    Spring.Echo("[METAL-TEST] Widget init, target=" .. TARGET_FRAMES)
end

function widget:Update(dt)
    -- Force-start the game after a short delay so simulation begins
    if not sentForceStart then
        local elapsed = Spring.DiffTimers(Spring.GetTimer(), startTimer)
        if elapsed > 2.0 then
            Spring.Echo("[METAL-TEST] Sending forcestart (elapsed=" .. string.format("%.1f", elapsed) .. "s)")
            Spring.SendCommands("forcestart")
            sentForceStart = true
        end
    end

    local gf = Spring.GetGameFrame()
    if gf < 0 then return end

    drawFrames = drawFrames + 1

    -- Screenshots at key draw frames
    for _, f in ipairs(screenshotAt) do
        if drawFrames == f and not taken[f] then
            Spring.SendCommands("screenshot png")
            Spring.Echo("[METAL-TEST] screenshot frame=" .. drawFrames .. " gameFrame=" .. gf)
            taken[f] = true
        end
    end

    -- Progress every 60 draw frames
    if drawFrames % 60 == 0 then
        local elapsed = Spring.DiffTimers(Spring.GetTimer(), startTimer)
        Spring.Echo(string.format("[METAL-TEST] progress frame=%d gf=%d t=%.1fs fps=%.1f",
            drawFrames, gf, elapsed, drawFrames / math.max(elapsed, 0.001)))
    end

    -- Exit
    if drawFrames >= TARGET_FRAMES then
        local elapsed = Spring.DiffTimers(Spring.GetTimer(), startTimer)
        Spring.Echo(string.format("[METAL-TEST] DONE frames=%d t=%.1fs fps=%.1f",
            drawFrames, elapsed, drawFrames / math.max(elapsed, 0.001)))
        Spring.Echo("[METAL-TEST] EXIT_SUCCESS")
        Spring.SendCommands("quitforce")
    end
end
