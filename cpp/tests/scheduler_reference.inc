void ClockModel::reference_advance(unsigned clocks, bool pause_for_refresh) {
    unsigned elapsed = 0;
    while (clocks) {
        const unsigned old = scanline_master_clock_;
        // Non-interlaced odd frames have one short scanline. Frame parity is
        // therefore part of timing, not merely a display presentation detail.
        const unsigned line_clocks =
            (scanline_index_ == 240 && (completed_frames & 1) && !(ppu_registers_[0x33] & 1)) ? 1360 : 1364;
        unsigned step = std::min(clocks, line_clocks - scanline_master_clock_);
        if (!refresh_done_ && scanline_master_clock_ < refresh_clock_)
            step = std::min(step, refresh_clock_ - scanline_master_clock_);
        scanline_master_clock_ += step;
        clocks -= step;
        master_clocks_ += step;
        elapsed += step;
        if (!refresh_done_ && scanline_master_clock_ >= refresh_clock_) {
            refresh_done_ = true;
            if (pause_for_refresh)
                clocks += 40;
        }
        if (autojoy_remaining_clocks_) {
            if (step >= autojoy_remaining_clocks_) {
                autojoy_remaining_clocks_ = 0;
                joy_result_ = buttons_;
            } else
                autojoy_remaining_clocks_ -= step;
        }
        const unsigned ht = (cpu_io_registers_[7] | ((cpu_io_registers_[8] & 1) << 8)) * 4;
        const unsigned vt = cpu_io_registers_[9] | ((cpu_io_registers_[10] & 1) << 8);
        const unsigned mode = cpu_io_registers_[0] & 0x30;
        if ((mode == 0x10 || (mode == 0x30 && scanline_index_ == vt)) && old <= ht &&
            scanline_master_clock_ > ht)
            irq_flag_ = true;
        if (scanline_index_ == 0 && old < 24 && scanline_master_clock_ >= 24)
            hdma_init();
        // Render from the register state for this line before HDMA prepares
        // the following line's effects. Visible output rows start at line 1.
        if (old < 1112 && scanline_master_clock_ >= 1112 && scanline_index_ <= 224) {
            if (scanline_index_ >= 1) {
                runtime_timing::Scope timing(runtime_timing::Phase::Ppu);
                render_scanline(scanline_index_ - 1);
            }
            hdma_line();
        }
        if (scanline_master_clock_ == line_clocks) {
            scanline_master_clock_ = 0;
            refresh_done_ = false;
            refresh_clock_ = unsigned(((master_clocks_ + 538) & ~uint64_t(7)) + 2 - master_clocks_);
            if (++scanline_index_ == 262) {
                scanline_index_ = 0;
                composition_policy_.completed(completed_frames);
                ++completed_frames;
                nmi_flag_ = false;
                if (!(ppu_registers_[0] & 0x80))
                    sprite_status_ = 0;
                // The visible scanlines are complete before this frame boundary.
                // Notify here, not after the CPU instruction: a DMA stall can
                // cross several frames before control returns to the frontend.
                if (on_presentation_frame)
                    on_presentation_frame(presentation_pixels(), presentation_width(), completed_frames);
            }
            if (scanline_index_ == 225) {
                nmi_flag_ = true;
                if (cpu_io_registers_[0] & 0x80)
                    nmi_pending_ = true;
                if (cpu_io_registers_[0] & 1)
                    autojoy_remaining_clocks_ = 4224;
                if (!(ppu_registers_[0] & 0x80))
                    oam_address_ = oam_reload_;
            }
            if (mode == 0x20 && scanline_index_ == vt)
                irq_flag_ = true;
        }
    }
    if (advance_audio_master_clocks)
        advance_audio_master_clocks(elapsed);
}
