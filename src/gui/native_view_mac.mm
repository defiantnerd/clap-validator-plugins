// macOS native view — plain Cocoa, no frameworks beyond AppKit. ARC enabled.
//
// Layout: one row per parameter (label | slider | value text) on top, below
// an NSTextView log pane (>=20 lines, monospaced) fed from the plugin's
// LogBuffer at ~10 Hz.

#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>

#include "gui/gui_model.h"
#include "gui/native_view.h"
#include "wrapper/logbuffer.h"

namespace {
constexpr CGFloat kLabelWidth = 120.0;
constexpr CGFloat kValueWidth = 90.0;
} // namespace

@interface CVPGuiView : NSView {
    cvp::GuiModel* _model;
    std::vector<cvp::GuiModel::ParamDesc> _params;
    std::vector<bool> _inGesture;
    NSMutableArray<NSSlider*>* _sliders;
    NSMutableArray<NSTextField*>* _nameLabels;
    NSMutableArray<NSTextField*>* _valueLabels;
    NSScrollView* _logScroll;
    NSTextView* _logView;
    NSTimer* _timer;
    uint64_t _logVersion;
}
- (instancetype)initWithModel:(cvp::GuiModel*)model;
- (void)shutdown;
@end

@implementation CVPGuiView

- (BOOL)isFlipped {
    return YES; // top-down layout: params first, log below
}

- (instancetype)initWithModel:(cvp::GuiModel*)model {
    const uint32_t paramCount = model->guiParamCount();
    const NSRect frame = NSMakeRect(0, 0, cvp::NativeView::kDefaultWidth,
                                    cvp::NativeView::minHeightFor(paramCount));
    self = [super initWithFrame:frame];
    if (!self)
        return nil;

    _model = model;
    _logVersion = UINT64_MAX; // force first render
    _sliders = [NSMutableArray array];
    _nameLabels = [NSMutableArray array];
    _valueLabels = [NSMutableArray array];

    for (uint32_t i = 0; i < paramCount; ++i) {
        cvp::GuiModel::ParamDesc desc{};
        if (!_model->guiParamDesc(i, &desc))
            continue;
        _params.push_back(desc);
        _inGesture.push_back(false);

        NSTextField* name = [NSTextField labelWithString:[NSString stringWithUTF8String:desc.name]];
        name.font = [NSFont systemFontOfSize:12];
        [_nameLabels addObject:name];
        [self addSubview:name];

        NSSlider* slider = [NSSlider sliderWithValue:_model->guiParamValue(desc.id)
                                            minValue:desc.minValue
                                            maxValue:desc.maxValue
                                              target:self
                                              action:@selector(sliderChanged:)];
        slider.continuous = YES;
        slider.tag = static_cast<NSInteger>(_params.size() - 1);
        if (desc.stepped) {
            slider.numberOfTickMarks =
                static_cast<NSInteger>(desc.maxValue - desc.minValue) + 1;
            slider.allowsTickMarkValuesOnly = YES;
        }
        [_sliders addObject:slider];
        [self addSubview:slider];

        NSTextField* value = [NSTextField labelWithString:@""];
        value.font = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];
        value.alignment = NSTextAlignmentRight;
        [_valueLabels addObject:value];
        [self addSubview:value];
    }

    _logView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
    _logView.editable = NO;
    _logView.richText = NO;
    NSFont* mono = nil;
    if (@available(macOS 10.15, *))
        mono = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    if (!mono)
        mono = [NSFont userFixedPitchFontOfSize:11];
    _logView.font = mono;
    _logView.backgroundColor = [NSColor textBackgroundColor];
    _logView.verticallyResizable = YES;
    _logView.horizontallyResizable = NO;
    _logView.autoresizingMask = NSViewWidthSizable;
    _logView.textContainer.widthTracksTextView = YES;

    _logScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
    _logScroll.hasVerticalScroller = YES;
    _logScroll.borderType = NSBezelBorder;
    _logScroll.documentView = _logView;
    [self addSubview:_logScroll];

    [self layoutNow];
    return self;
}

- (void)layoutNow {
    const CGFloat pad = cvp::NativeView::kPadding;
    const CGFloat rowH = cvp::NativeView::kRowHeight;
    const CGFloat width = self.frame.size.width;
    const CGFloat height = self.frame.size.height;

    for (size_t i = 0; i < _params.size(); ++i) {
        const CGFloat y = pad + static_cast<CGFloat>(i) * rowH;
        NSSlider* slider = _sliders[i];
        NSTextField* name = _nameLabels[i];
        NSTextField* value = _valueLabels[i];

        name.frame = NSMakeRect(pad, y + 4, kLabelWidth, 17);
        slider.frame =
            NSMakeRect(pad + kLabelWidth + 6, y + 2,
                       width - kLabelWidth - kValueWidth - 3 * pad - 12, rowH - 6);
        value.frame = NSMakeRect(width - kValueWidth - pad, y + 4, kValueWidth, 17);
    }

    const CGFloat logTop = pad + static_cast<CGFloat>(_params.size()) * rowH + pad;
    _logScroll.frame = NSMakeRect(pad, logTop, width - 2 * pad, height - logTop - pad);
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self layoutNow];
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (self.window && !_timer) {
        __weak CVPGuiView* weakSelf = self;
        _timer = [NSTimer timerWithTimeInterval:0.1
                                        repeats:YES
                                          block:^(NSTimer*) {
                                              [weakSelf tick];
                                          }];
        // Common modes: default-mode timers stall during drags/tracking.
        [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
    }
}

- (void)viewWillMoveToWindow:(NSWindow*)newWindow {
    [super viewWillMoveToWindow:newWindow];
    if (!newWindow) {
        // Host tore the window down (with or without gui->destroy).
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)tick {
    if (!_model)
        return;

    char text[96];
    for (size_t i = 0; i < _params.size(); ++i) {
        const double value = _model->guiParamValue(_params[i].id);
        _model->guiParamValueText(_params[i].id, value, text, sizeof(text));
        _valueLabels[i].stringValue = [NSString stringWithUTF8String:text];
        if (!_inGesture[i]) // reflect host-side automation while idle
            _sliders[i].doubleValue = value;
    }

    const uint64_t version = _model->guiLog().version();
    if (version != _logVersion) {
        _logVersion = version;
        const auto lines = _model->guiLog().snapshot();
        std::string joined;
        joined.reserve(lines.size() * 64);
        for (const auto& line : lines) {
            joined += line;
            joined += '\n';
        }
        _logView.string = [NSString stringWithUTF8String:joined.c_str()];
        [_logView scrollRangeToVisible:NSMakeRange(_logView.string.length, 0)];
    }
}

- (void)sliderChanged:(NSSlider*)sender {
    const auto index = static_cast<size_t>(sender.tag);
    if (index >= _params.size() || !_model)
        return;
    const clap_id paramId = _params[index].id;
    const NSEventType eventType = NSApp.currentEvent.type;

    if (eventType == NSEventTypeLeftMouseDown || eventType == NSEventTypeLeftMouseDragged) {
        if (!_inGesture[index]) {
            _inGesture[index] = true;
            _model->guiBeginGesture(paramId);
        }
        _model->guiSetValue(paramId, sender.doubleValue);
    } else if (eventType == NSEventTypeLeftMouseUp) {
        if (!_inGesture[index]) {
            _inGesture[index] = true;
            _model->guiBeginGesture(paramId);
        }
        _model->guiSetValue(paramId, sender.doubleValue);
        _model->guiEndGesture(paramId);
        _inGesture[index] = false;
    } else {
        // Keyboard or other non-mouse change: atomic begin+value+end.
        _model->guiBeginGesture(paramId);
        _model->guiSetValue(paramId, sender.doubleValue);
        _model->guiEndGesture(paramId);
    }
}

- (void)shutdown {
    [_timer invalidate];
    _timer = nil;
    _model = nullptr;
    [self removeFromSuperview];
}

@end

namespace cvp {

namespace {

class MacNativeView final : public NativeView {
public:
    explicit MacNativeView(GuiModel& model) {
        _view = [[CVPGuiView alloc] initWithModel:&model];
    }

    ~MacNativeView() override {
        [_view shutdown];
        _view = nil;
    }

    bool attach(const clap_window* parent) noexcept override {
        if (!_view || !parent || !parent->cocoa)
            return false;
        NSView* parentView = (__bridge NSView*)parent->cocoa;
        [parentView addSubview:_view];
        _view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        if (_pendingSize.width > 0)
            [_view setFrameSize:_pendingSize];
        return true;
    }

    void getSize(uint32_t* width, uint32_t* height) noexcept override {
        const NSSize size = _view ? _view.frame.size : NSMakeSize(kDefaultWidth, 0);
        *width = static_cast<uint32_t>(size.width);
        *height = static_cast<uint32_t>(size.height);
    }

    bool setSize(uint32_t width, uint32_t height) noexcept override {
        const NSSize size = NSMakeSize(width, height);
        if (_view.superview == nil)
            _pendingSize = size; // apply at attach
        [_view setFrameSize:size];
        return true;
    }

    void setScale(double) noexcept override {
        // Cocoa is logical-size; retina scaling is automatic.
    }

    void show() noexcept override { _view.hidden = NO; }
    void hide() noexcept override { _view.hidden = YES; }

private:
    CVPGuiView* _view = nil; // strong under ARC
    NSSize _pendingSize = NSMakeSize(0, 0);
};

} // namespace

std::unique_ptr<NativeView> createNativeView(GuiModel& model) {
    return std::make_unique<MacNativeView>(model);
}

} // namespace cvp
