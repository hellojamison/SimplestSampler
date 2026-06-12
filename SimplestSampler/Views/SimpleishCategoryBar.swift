import SwiftUI
import UniformTypeIdentifiers

struct SimpleishCategoryBar: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme

    @State private var showAddCategory = false
    @State private var newCategoryName = ""
    @State private var renamingCategory: StoredCategory?
    @State private var renameDraft = ""

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                filterChip(title: "All", filter: SimpleishCategoryFilter.all, acceptsDrop: false)

                filterChip(
                    title: "Uncategorized",
                    filter: SimpleishCategoryFilter.uncategorized,
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
                .buttonStyle(SimpleishCategoryChipStyle(isActive: false))
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
            filter: filter,
            acceptsDrop: acceptsDrop,
            isActive: viewModel.simpleishCategoryFilter == filter,
            onSelect: { viewModel.setSimpleishCategoryFilter(filter) },
            onDropCapture: { captureId in
                assignCapture(captureId, to: filter)
            }
        )
    }

    private func assignCapture(_ captureId: String, to filter: String) {
        switch filter {
        case SimpleishCategoryFilter.all:
            break
        case SimpleishCategoryFilter.uncategorized:
            viewModel.setCaptureCategory(captureId: captureId, categoryId: nil)
        default:
            viewModel.setCaptureCategory(captureId: captureId, categoryId: filter)
        }
    }
}

private struct CategoryFilterChip: View {
    let title: String
    let filter: String
    let acceptsDrop: Bool
    let isActive: Bool
    let onSelect: () -> Void
    let onDropCapture: (String) -> Void

    @State private var isDropTargeted = false

    var body: some View {
        Button(title, action: onSelect)
            .buttonStyle(SimpleishCategoryChipStyle(isActive: isActive, isDropTarget: isDropTargeted))
            .onDrop(of: [.simplestSamplerStoredCapture, .plainText], isTargeted: $isDropTargeted) { providers in
                guard acceptsDrop else { return false }
                return handleDrop(providers)
            }
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: {
            $0.hasItemConformingToTypeIdentifier(UTType.simplestSamplerStoredCapture.identifier)
                || $0.hasItemConformingToTypeIdentifier(UTType.plainText.identifier)
        }) else {
            return false
        }

        if provider.hasItemConformingToTypeIdentifier(UTType.simplestSamplerStoredCapture.identifier) {
            provider.loadItem(forTypeIdentifier: UTType.simplestSamplerStoredCapture.identifier, options: nil) { item, _ in
                guard let captureId = captureId(from: item) else { return }
                Task { @MainActor in onDropCapture(captureId) }
            }
            return true
        }

        provider.loadObject(ofClass: NSString.self) { item, _ in
            guard let captureId = item as? String, captureId.hasPrefix("sampler-stored-") else { return }
            Task { @MainActor in onDropCapture(captureId) }
        }
        return true
    }

    private func captureId(from item: NSSecureCoding?) -> String? {
        if let string = item as? String { return string }
        if let data = item as? Data { return String(data: data, encoding: .utf8) }
        if let url = item as? URL { return url.absoluteString }
        return nil
    }
}

struct SimpleishCategoryChipStyle: ButtonStyle {
    var isActive: Bool
    var isDropTarget: Bool = false
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 11, weight: .semibold))
            .padding(.horizontal, 10)
            .frame(minHeight: 24)
            .foregroundStyle(isActive || isDropTarget ? theme.slotSelectedText : theme.muted)
            .background(
                Capsule(style: .continuous)
                    .fill(chipFill)
            )
            .overlay(
                Capsule(style: .continuous)
                    .stroke(isDropTarget ? theme.accent : theme.border, lineWidth: isDropTarget ? 2 : (isActive ? 0 : 1))
            )
            .opacity(configuration.isPressed ? 0.85 : 1)
    }

    private var chipFill: Color {
        if isDropTarget {
            return theme.accentSoft.opacity(0.9)
        }
        return isActive ? theme.accent : theme.chipBackground
    }
}

struct SimpleishEmptyFilterRow: View {
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
