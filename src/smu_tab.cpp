#include "smu_tab.h"

SmuTab::SmuTab(Node &nil)
	: SmuTree(nil)

{
}

SmuTab::~SmuTab()
{
}

int SmuTab::compare(std::span<std::byte> a, std::span<std::byte> b)
{
	if (a.data() < b.data())
		return -1;
	if (a.data() > b.data())
		return 1;
	return 0;
}

void SmuTab::collision(const Node *node, std::span<std::byte> &key)
{
	/** @todo later */
}

bool SmuTab::push(std::span<SmuTree::Node> nodes)
{
	if (nodes.empty())
		return false;

	if (!writeMeta(nodes[0].keyData, reinterpret_cast<uintptr_t>(&nodes[0]),
		       nodes.size())) {
		return false;
	}

	bool result = true;
	for (size_t i = 0; i < nodes.size(); ++i) {
		if (!this->insert(&nodes[i], nodes[i].keyData)) {
			result = false;
		}
	}

	return result;
}

bool SmuTab::writeMeta(std::span<std::byte> data, uintptr_t nodeAddr,
		       size_t count)
{
	if (data.size_bytes() < sizeof(MetaHead)) {
		return false;
	}

	MetaHead *head = reinterpret_cast<MetaHead *>(data.data());

	head->node = nodeAddr;
	head->nodeCount = count;
	head->xorMeta = head->node ^ head->nodeCount;

	return true;
}

SmuTab::MetaHead *SmuTab::readMeta(std::span<std::byte> data) const
{
	if (data.size_bytes() < sizeof(MetaHead)) {
		return nullptr;
	}

	MetaHead *head = reinterpret_cast<MetaHead *>(data.data());

	if (head->xorMeta != (head->node ^ head->nodeCount)) {
		return nullptr;
	}

	return head;
}

std::span<SmuTree::Node> SmuTab::pop(std::span<std::byte> a)
{
	SmuTree::Node *masterNode = this->find(a);
	if (!masterNode || masterNode->keyData.data() != a.data())
		return {};

	MetaHead *meta = readMeta(masterNode->keyData);
	if (!meta)
		return {};

	SmuTree::Node *firstNode =
		reinterpret_cast<SmuTree::Node *>(meta->node);
	size_t count = meta->nodeCount;

	SmuTree::Node *current = firstNode;
	for (size_t i = 0; i < count; ++i) {
		if (this->remove(current->keyData) == false)
			return {};
		current++;
	}

	return std::span<SmuTree::Node>(firstNode, count);
}
