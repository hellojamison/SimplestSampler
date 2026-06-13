#import <Cocoa/Cocoa.h>

#include "SimplestSampler_AS_GUI.h"
#include "SimplestSampler_AS_Parameters.h"
#include "SimplestSampler_AS_Defs.h"

#include "AAX_CString.h"
#include "AAX_IEffectParameters.h"
#include "AAX_IParameter.h"
#include "AAX_UtilsNative.h"

namespace {

constexpr CGFloat kPanelWidth = 300.0;
constexpr CGFloat kPanelHeight = 340.0;
constexpr CGFloat kRowHeight = 44.0;
constexpr CGFloat kPadding = 10.0;

NSColor* ThemeBackground() {
    return [NSColor colorWithCalibratedRed:0.196 green:0.184 blue:0.175 alpha:1.0];
}

NSColor* ThemeSlotBackground() {
    return [NSColor colorWithCalibratedRed:0.251 green:0.235 blue:0.224 alpha:0.92];
}

NSColor* ThemeSlotSelected() {
    return [NSColor colorWithCalibratedRed:0.204 green:0.431 blue:0.337 alpha:0.96];
}

NSColor* ThemeText() {
    return [NSColor colorWithCalibratedRed:0.867 green:0.843 blue:0.820 alpha:1.0];
}

NSColor* ThemeMuted() {
    return [NSColor colorWithCalibratedRed:0.702 green:0.678 blue:0.655 alpha:1.0];
}

NSColor* ThemeAccent() {
    return [NSColor colorWithCalibratedRed:0.173 green:0.478 blue:0.373 alpha:1.0];
}

SimplestSampler_AS_Parameters* SamplerParameters(AAX_IEffectParameters* parameters) {
    return dynamic_cast<SimplestSampler_AS_Parameters*>(parameters);
}

const SimplestSampler_AS_Parameters* SamplerParameters(const AAX_IEffectParameters* parameters) {
    return dynamic_cast<const SimplestSampler_AS_Parameters*>(parameters);
}

} // namespace

@interface SimplestSamplerSlotRowView : NSControl
@property(nonatomic, assign) NSInteger slotNumber;
@property(nonatomic, assign) BOOL selected;
@property(nonatomic, copy) NSString* titleText;
@property(nonatomic, copy) NSString* detailText;
@end

@implementation SimplestSamplerSlotRowView

- (instancetype)initWithSlotNumber:(NSInteger)slotNumber {
    self = [super initWithFrame:NSMakeRect(0, 0, kPanelWidth - (kPadding * 2.0), kRowHeight)];
    if (self) {
        _slotNumber = slotNumber;
        _selected = NO;
        self.wantsLayer = YES;
    }
    return self;
}

- (void)setSelected:(BOOL)selected {
    _selected = selected;
    self.needsDisplay = YES;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    const NSRect bounds = self.bounds;
    const CGFloat radius = 11.0;
    NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.5, 0.5)
                                                         xRadius:radius
                                                         yRadius:radius];
    [(_selected ? ThemeSlotSelected() : ThemeSlotBackground()) setFill];
    [path fill];

    NSDictionary* numberAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: _selected ? ThemeText() : ThemeMuted()
    };
    NSDictionary* titleAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: ThemeText()
    };
    NSDictionary* detailAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: _selected ? [ThemeText() colorWithAlphaComponent:0.72] : ThemeMuted()
    };

    const NSString* number = [NSString stringWithFormat:@"%ld", (long)_slotNumber];
    [number drawInRect:NSMakeRect(10, 8, 18, 16) withAttributes:numberAttrs];
    const NSString* title = _titleText ?: @"Empty";
    [title drawInRect:NSMakeRect(30, 6, bounds.size.width - 40, 18) withAttributes:titleAttrs];
    const NSString* detail = _detailText ?: @"";
    [detail drawInRect:NSMakeRect(30, 24, bounds.size.width - 40, 14) withAttributes:detailAttrs];
}

- (void)mouseDown:(NSEvent*)event {
    (void)event;
    [self sendAction:self.action to:self.target];
}

@end

@interface SimplestSamplerPanelView : NSView
@property(nonatomic, assign) SimplestSampler_AS_GUI* gui;
@property(nonatomic, assign, getter=isDetachedFromGUI) BOOL detachedFromGUI;
@property(nonatomic, assign, getter=isReadyForRefresh) BOOL readyForRefresh;
@property(nonatomic, assign) uint64_t refreshEpoch;
@property(nonatomic, strong) NSPopUpButton* activeSlotsPopup;
@property(nonatomic, strong) NSArray<SimplestSamplerSlotRowView*>* slotRowsSnapshot;
- (void)detachFromGUI;
- (void)refreshOnMainThread;
@end

@implementation SimplestSamplerPanelView

- (instancetype)initWithGUI:(SimplestSampler_AS_GUI*)gui {
    self = [super initWithFrame:NSMakeRect(0, 0, kPanelWidth, kPanelHeight)];
    if (self) {
        _gui = gui;
        _detachedFromGUI = NO;
        _readyForRefresh = NO;
        _refreshEpoch = 0;
        NSMutableArray<SimplestSamplerSlotRowView*>* slotRows = [NSMutableArray array];
        self.wantsLayer = YES;
        self.layer.backgroundColor = ThemeBackground().CGColor;

        const CGFloat contentWidth = kPanelWidth - (kPadding * 2.0);

        NSTextField* title = [self makeLabel:@"SimplestSampler" size:15 weight:NSFontWeightBold];
        title.frame = NSMakeRect(kPadding, kPadding, contentWidth, 20);
        [self addSubview:title];

        NSTextField* subtitle = [self makeLabel:@"Click a slot to choose capture target" size:11 weight:NSFontWeightRegular];
        subtitle.textColor = ThemeMuted();
        subtitle.frame = NSMakeRect(kPadding, kPadding + 22, contentWidth, 16);
        [self addSubview:subtitle];

        NSTextField* activeLabel = [self makeLabel:@"Active slots" size:11 weight:NSFontWeightSemibold];
        activeLabel.frame = NSMakeRect(kPadding, kPadding + 44, 90, 18);
        [self addSubview:activeLabel];

        _activeSlotsPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(kPadding + 92, kPadding + 40, 80, 24)
                                                       pullsDown:NO];
        for (NSInteger count = kSimplestSamplerDefaultActiveSlots; count <= kSimplestSamplerMaxActiveSlots; ++count) {
            [_activeSlotsPopup addItemWithTitle:[NSString stringWithFormat:@"%ld", (long)count]];
        }
        _activeSlotsPopup.target = self;
        _activeSlotsPopup.action = @selector(activeSlotsChanged:);
        [self addSubview:_activeSlotsPopup];

        CGFloat y = kPadding + 72;
        for (NSInteger slot = 1; slot <= kSimplestSamplerMaxActiveSlots; ++slot) {
            SimplestSamplerSlotRowView* row = [[SimplestSamplerSlotRowView alloc] initWithSlotNumber:slot];
            row.frame = NSMakeRect(kPadding, y, contentWidth, kRowHeight);
            row.target = self;
            row.action = @selector(slotRowClicked:);
            row.tag = slot;
            [self addSubview:row];
            [slotRows addObject:row];
            y += kRowHeight + 6.0;
        }
        _slotRowsSnapshot = [slotRows copy];

        NSTextField* hint = [self makeLabel:@"Use Capture in AudioSuite to write the selected slot."
                                       size:10
                                     weight:NSFontWeightRegular];
        hint.textColor = ThemeMuted();
        hint.frame = NSMakeRect(kPadding, kPanelHeight - 28, contentWidth, 16);
        [self addSubview:hint];

        _readyForRefresh = YES;
    }
    return self;
}

- (void)dealloc {
    ++_refreshEpoch;
    _readyForRefresh = NO;
    _gui = nullptr;
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(refresh) object:nil];
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(refreshOnMainThread) object:nil];
    [super dealloc];
}

- (void)detachFromGUI {
    ++_refreshEpoch;
    _readyForRefresh = NO;
    _detachedFromGUI = YES;
    _gui = nullptr;
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(refresh) object:nil];
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(refreshOnMainThread) object:nil];
}

- (NSTextField*)makeLabel:(NSString*)text size:(CGFloat)size weight:(NSFontWeight)weight {
    NSTextField* label = [[NSTextField alloc] initWithFrame:NSZeroRect];
    label.stringValue = text;
    label.bezeled = NO;
    label.drawsBackground = NO;
    label.editable = NO;
    label.selectable = NO;
    label.font = [NSFont systemFontOfSize:size weight:weight];
    label.textColor = ThemeText();
    return label;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)activeSlotsChanged:(id)sender {
    (void)sender;
    if (self.isDetachedFromGUI || !_gui) {
        return;
    }
    const NSInteger count = [_activeSlotsPopup indexOfSelectedItem] + kSimplestSamplerDefaultActiveSlots;
    _gui->SetActiveSlotCount(static_cast<int32_t>(count));
    [self refresh];
}

- (void)slotRowClicked:(SimplestSamplerSlotRowView*)sender {
    if (self.isDetachedFromGUI || !_gui || !sender) {
        return;
    }
    _gui->SetTargetSlot(static_cast<int32_t>(sender.tag));
    [self refresh];
}

- (void)refresh {
    if (!self.isReadyForRefresh || self.isDetachedFromGUI || !_gui || self.superview == nil) {
        return;
    }
    if (![NSThread isMainThread]) {
        if (SimplestSampler_AS_GUI* gui = _gui) {
            gui->RefreshPanel();
        }
        return;
    }
    [self refreshOnMainThread];
}

- (void)refreshOnMainThread {
    if (!self.isReadyForRefresh || self.isDetachedFromGUI || !_gui || self.superview == nil) {
        return;
    }

    NSPopUpButton* const popup = _activeSlotsPopup;
    NSArray<SimplestSamplerSlotRowView*>* const rows = _slotRowsSnapshot;
    SimplestSampler_AS_GUI* const gui = _gui;

    if (!popup || rows.count == 0 || !gui) {
        return;
    }

    const int32_t activeCount = gui->ActiveSlotCount();
    const int32_t targetSlot = gui->TargetSlot();
    const NSInteger popupIndex = activeCount - kSimplestSamplerDefaultActiveSlots;
    if (popupIndex >= 0 && popupIndex < popup.numberOfItems) {
        [popup selectItemAtIndex:popupIndex];
    }

    auto* samplerParameters = SamplerParameters(gui->GetEffectParameters());

    for (SimplestSamplerSlotRowView* row in rows) {
        if (!row || ![row isKindOfClass:[SimplestSamplerSlotRowView class]]) {
            continue;
        }
        const BOOL visible = row.slotNumber <= activeCount;
        [row setHidden:!visible];
        row.selected = (row.slotNumber == targetSlot);

        if (!visible || !samplerParameters) {
            continue;
        }

        const std::string label = samplerParameters->SlotDisplayLabel(static_cast<int>(row.slotNumber));
        const size_t pipe = label.find(" | Cap ");
        if (pipe == std::string::npos) {
            row.titleText = [NSString stringWithUTF8String:label.c_str()] ?: @"Empty";
            row.detailText = @"";
        } else {
            row.titleText = [NSString stringWithUTF8String:label.substr(0, pipe).c_str()] ?: @"Empty";
            row.detailText = [NSString stringWithUTF8String:label.substr(pipe + 3).c_str()] ?: @"";
        }
        [row setNeedsDisplay:YES];
    }
}

@end

AAX_CEffectGUI* AAX_CALLBACK SimplestSampler_AS_GUI::Create() {
    return new SimplestSampler_AS_GUI();
}

SimplestSamplerPanelView* SimplestSampler_AS_GUI::PanelView() const {
    if (!mViewController) {
        return nullptr;
    }

    NSView* const view = mViewController.view;
    if (!view || ![view isKindOfClass:[SimplestSamplerPanelView class]]) {
        return nullptr;
    }

    return (SimplestSamplerPanelView*)view;
}

SimplestSampler_AS_GUI::~SimplestSampler_AS_GUI() {
    mPanelRefreshEnabled = false;
    DetachPanelView();
}

void SimplestSampler_AS_GUI::CreateViewContents() {
    SimplestSamplerPanelView* const panelView = [[SimplestSamplerPanelView alloc] initWithGUI:this];
    NSViewController* viewController = [[NSViewController alloc] init];
    viewController.view = panelView;
    SetViewController(viewController);
}

void SimplestSampler_AS_GUI::CreateViewContainer() {
    mPanelRefreshEnabled = false;

    if (!PanelView()) {
        CreateViewContents();
    }

    AAX_CEffectGUI_Cocoa::CreateViewContainer();

    if (SimplestSamplerPanelView* panelView = PanelView()) {
        if (panelView.superview != nil) {
            mPanelRefreshEnabled = true;
            [panelView refresh];
        }
    }
}

void SimplestSampler_AS_GUI::DeleteViewContainer() {
    mPanelRefreshEnabled = false;
    if (SimplestSamplerPanelView* panelView = PanelView()) {
        [NSObject cancelPreviousPerformRequestsWithTarget:panelView selector:@selector(refresh) object:nil];
        [NSObject cancelPreviousPerformRequestsWithTarget:panelView selector:@selector(refreshOnMainThread) object:nil];
    }
    DetachPanelView();
    AAX_CEffectGUI_Cocoa::DeleteViewContainer();
}

void SimplestSampler_AS_GUI::SchedulePanelRefresh() {
    if (!mPanelRefreshEnabled || !GetViewContainer()) {
        return;
    }

    SimplestSamplerPanelView* const panelView = PanelView();
    if (!panelView || panelView.superview == nil || panelView.isDetachedFromGUI || !panelView.isReadyForRefresh) {
        return;
    }

    const uint64_t panelEpoch = panelView.refreshEpoch;
    [panelView retain];

    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            if (panelView.refreshEpoch != panelEpoch
                || panelView.isDetachedFromGUI
                || !panelView.isReadyForRefresh
                || panelView.superview == nil) {
                [panelView release];
                return;
            }
            [panelView refreshOnMainThread];
            [panelView release];
        }
    });
}

void SimplestSampler_AS_GUI::DetachPanelView() {
    if (SimplestSamplerPanelView* panelView = PanelView()) {
        [panelView detachFromGUI];
    }
}

AAX_Result SimplestSampler_AS_GUI::GetViewSize(AAX_Point* oEffectViewSize) const {
    if (!oEffectViewSize) {
        return AAX_ERROR_NULL_ARGUMENT;
    }
    oEffectViewSize->horz = kPanelWidth;
    oEffectViewSize->vert = kPanelHeight;
    return AAX_SUCCESS;
}

AAX_Result SimplestSampler_AS_GUI::GetCustomLabel(AAX_EPlugInStrings iSelector, AAX_IString* oString) const {
    if (!oString) {
        return AAX_ERROR_NULL_OBJECT;
    }

    if (iSelector == AAX_ePlugInStrings_Analysis) {
        *oString = AAX_CString("Capture");
        return AAX_SUCCESS;
    }

    return AAX_CEffectGUI::GetCustomLabel(iSelector, oString);
}

AAX_Result SimplestSampler_AS_GUI::ParameterUpdated(AAX_CParamID iParameterID) {
    if (AAX::IsParameterIDEqual(iParameterID, kSimplestSamplerParamTargetSlot)
        || AAX::IsParameterIDEqual(iParameterID, kSimplestSamplerParamActiveSlotCount)) {
        RefreshPanel();
    }
    return AAX_SUCCESS;
}

AAX_Result SimplestSampler_AS_GUI::TimerWakeup() {
    SchedulePanelRefresh();
    return AAX_SUCCESS;
}

void SimplestSampler_AS_GUI::RefreshPanel() {
    SchedulePanelRefresh();
}

bool SimplestSampler_AS_GUI::SetTargetSlot(int32_t slot) {
    if (auto* parameters = SamplerParameters(GetEffectParameters())) {
        return parameters->SetTargetSlotNumber(slot);
    }
    return false;
}

bool SimplestSampler_AS_GUI::SetActiveSlotCount(int32_t count) {
    if (auto* parameters = SamplerParameters(GetEffectParameters())) {
        return parameters->SetActiveSlotCount(count);
    }
    return false;
}

int32_t SimplestSampler_AS_GUI::TargetSlot() const {
    if (auto* parameters = SamplerParameters(GetEffectParameters())) {
        return parameters->TargetSlotNumber();
    }
    return 1;
}

int32_t SimplestSampler_AS_GUI::ActiveSlotCount() const {
    if (auto* parameters = SamplerParameters(GetEffectParameters())) {
        return parameters->ActiveSlotCount();
    }
    return kSimplestSamplerDefaultActiveSlots;
}
