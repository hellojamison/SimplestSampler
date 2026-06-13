import SwiftUI
import UniformTypeIdentifiers

struct StoredCategoryBar: View {
    @ObservedObject var viewModel: SamplerViewModel

    @State private var showAddCategory = false
    @State private var newCategoryName = ""
    @State private var renamingCategory: StoredCategory?
    @State private var renameDraft = ""

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                filterChip(title: "All", filter: StoredCategoryFilter.all, acceptsDrop: false)

                filterChip(
                    title: "Uncategorized",
                    filter: StoredCategoryFilter.uncategorized,
                    acceptsDrop: true
                )

                ForEach(viewModel.storedCategories) { category in
                    filterChip(title: category.name, filter: category.id, acceptsDrop: true)
                        .contextMenu {
                            Button("Rename") {
                                renamingCategory = category
                                renameDraft = category.name
                            }
                            Button("Delete", role: .destructive) {
                                viewModel.deleteCategory(id: category.id)
                            }
                        }
                }

                Button {
                    newCategoryName = ""
                    showAddCategory = true
                } label: {
                    Image(systemName: "plus")
                        .font(.system(size: 11, weight: .bold))
                        .frame(width: 24, height: 24)
                }
                .buttonStyle(StoredCategoryChipStyle(isActive: false))
                .help("Add category")
            }
            .padding(.horizontal, 2)
        }
        .alert("New Category", isPresented: $showAddCategory) {
            TextField("Name", text: $newCategoryName)
            Button("Add") {
                viewModel.addCategory(name: newCategoryName)
            }
            Button("Cancel", role: .cancel) {}
        }
        .alert("Rename Category", isPresented: Binding(
            get: { renamingCategory != nil },
            set: { if !$0 { renamingCategory = nil } }
        )) {
            TextField("Name", text: $renameDraft)
            Button("Save") {
                if let category = renamingCategory {
                    viewModel.renameCategory(id: category.id, name: renameDraft)
                }
                renamingCategory = nil
            }
            Button("Cancel", role: .cancel) {
                renamingCategory = nil
            }
        }
    }

    private func filterChip(title: String, filter: String, acceptsDrop: Bool) -> some View {
        CategoryFilterChip(
            title: title,
            acceptsDrop: acceptsDrop,
            isActive: viewModel.storedCategoryFilter == filter,
            onSelect: { viewModel.setStoredCategoryFilter(filter) },
            onDropItem: { item in
                handleDropItem(item, for: filter)
            }
        )
    }

    private func assignCapture(_ captureId: String, to filter: String) {
        switch filter {
        case StoredCategoryFilter.all:
            break
        case StoredCategoryFilter.uncategorized:
            viewModel.setCaptureCategory(captureId: captureId, categoryId: nil)
        default:
            viewModel.setCaptureCategory(captureId: captureId, categoryId: filter)
        }
    }

    private func handleDropItem(_ item: CategoryChipDropItem, for filter: String) {
        switch item {
        case .storedCapture(let captureId):
            assignCapture(captureId, to: filter)
        case .activeSlot(let slotIndex):
            switch filter {
            case StoredCategoryFilter.all:
                viewModel.storeActiveCapture(from: slotIndex)
            case StoredCategoryFilter.uncategorized:
                viewModel.storeActiveCapture(from: slotIndex, categoryId: nil)
            default:
                viewModel.storeActiveCapture(from: slotIndex, categoryId: filter)
            }
        }
    }
}

private struct CategoryFilterChip: View {
    let title: String
    let acceptsDrop: Bool
    let isActive: Bool
    let onSelect: () -> Void
    let onDropItem: (CategoryChipDropItem) -> Void

    @State private var isDropTargeted = false

    var body: some View {
        Button(title, action: onSelect)
            .buttonStyle(StoredCategoryChipStyle(isActive: isActive, isDropTarget: isDropTargeted))
            .onDrop(of: [.simplestSamplerStoredCapture, .simplestSamplerActiveSlotDrag, .simplestSamplerActiveSlotIndex, .plainText], isTargeted: $isDropTargeted) { providers in
                guard acceptsDrop else { return false }
                return handleDrop(providers)
            }
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        if let provider = providers.first(where: {
            $0.hasItemConformingToTypeIdentifier(UTType.simplestSamplerStoredCapture.identifier)
        }) {
            provider.loadStoredCaptureID { captureId in
                guard let captureId, captureId.hasPrefix("sampler-stored-") else { return }
                Task { @MainActor in onDropItem(.storedCapture(captureId)) }
            }
            return true
        }

        if let provider = providers.first(where: {
            $0.hasItemConformingToTypeIdentifier(UTType.simplestSamplerActiveSlotDrag.identifier)
                || $0.hasItemConformingToTypeIdentifier(UTType.simplestSamplerActiveSlotIndex.identifier)
        }) {
            provider.loadActiveSlotDragPayload { payload in
                guard let payload else { return }
                Task { @MainActor in onDropItem(.activeSlot(payload.slotIndex)) }
            }
            return true
        }

        if let provider = providers.first(where: {
            $0.hasItemConformingToTypeIdentifier(UTType.plainText.identifier)
        }) {
            provider.loadObject(ofClass: NSString.self) { item, _ in
                guard let rawValue = item as? String else { return }

                if rawValue.hasPrefix("sampler-stored-") {
                    Task { @MainActor in onDropItem(.storedCapture(rawValue)) }
                    return
                }

                guard let payload = ActiveSlotDragPayload.decodeLegacyString(rawValue) else { return }
                Task { @MainActor in onDropItem(.activeSlot(payload.slotIndex)) }
            }
            return true
        }

        return false
    }
}

private enum CategoryChipDropItem {
    case storedCapture(String)
    case activeSlot(Int)
}

struct StoredCategoryChipStyle: ButtonStyle {
    var isActive: Bool
    var isDropTarget: Bool = false
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 11, weight: .semibold))
            .padding(.horizontal, 10)
            .frame(minHeight: 24)
            .foregroundStyle(isActive || isDropTarget ? theme.selectedRowText : theme.muted)
            .background(
                Capsule(style: .continuous)
                    .fill(chipFill)
            )
            .overlay(
                Capsule(style: .continuous)
                    .stroke(
                        isDropTarget ? theme.accent.opacity(0.58) : (isActive ? theme.selectedRowBorder : theme.border),
                        lineWidth: isDropTarget ? 2 : 1
                    )
            )
            .shadow(color: isActive ? theme.accent.opacity(0.14) : .clear, radius: 4, y: 1)
            .opacity(configuration.isPressed ? 0.85 : 1)
    }

    private var chipFill: LinearGradient {
        if isDropTarget {
            return LinearGradient(
                colors: [
                    theme.accentSoft.opacity(0.98),
                    theme.accentSoft.opacity(0.88)
                ],
                startPoint: .top,
                endPoint: .bottom
            )
        }
        if isActive {
            return theme.accentGradient
        }
        return LinearGradient(
            colors: [theme.chipBackground, theme.chipBackground.opacity(0.9)],
            startPoint: .top,
            endPoint: .bottom
        )
    }
}

struct StoredEmptyFilterRow: View {
    @Environment(\.samplerThemeColors) private var theme

    var body: some View {
        Text("No samples in this category.")
            .font(.system(size: 12))
            .foregroundStyle(theme.muted)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, SamplerTheme.Layout.rowPaddingH)
            .padding(.vertical, SamplerTheme.Layout.rowPaddingV)
            .samplerChipSurface()
    }
}
