import AppKit
import SwiftUI

struct SamplerVolumeSlider: NSViewRepresentable {
    @Binding var value: Int
    var maxValue: Int

    func makeCoordinator() -> Coordinator {
        Coordinator(value: $value)
    }

    func makeNSView(context: Context) -> NSSlider {
        let slider = NSSlider(
            value: Double(value),
            minValue: 0,
            maxValue: Double(maxValue),
            target: context.coordinator,
            action: #selector(Coordinator.valueChanged(_:))
        )
        slider.isContinuous = true
        slider.numberOfTickMarks = 0
        slider.controlSize = .small
        return slider
    }

    func updateNSView(_ slider: NSSlider, context: Context) {
        let next = Double(value)
        guard abs(slider.doubleValue - next) > 0.5 else { return }
        slider.doubleValue = next
    }

    final class Coordinator: NSObject {
        private var value: Binding<Int>

        init(value: Binding<Int>) {
            self.value = value
        }

        @objc func valueChanged(_ sender: NSSlider) {
            value.wrappedValue = Int(sender.doubleValue.rounded())
        }
    }
}
