#include "smu_tree.h"

SmuTree::SmuTree(Node &nilMem)
{
	nil = &nilMem;
	nil->color = Color::Black;
	nil->left = nil;
	nil->right = nil;
	nil->parent = nil;

	root = nil;

	stats.nodeCount.store(0, std::memory_order_relaxed);
	stats.treeSize.store(sizeof(Node), std::memory_order_relaxed);
	stats.keyDataSize.store(0, std::memory_order_relaxed);
}

SmuTree::Node *SmuTree::find(std::span<std::byte> key)
{
	Node *x = root;
	while (x != nil) {
		int res = compare(key, x->keyData);
		if (res == 0)
			return x;

		x = (res < 0) ? x->left : x->right;
	}
	return nullptr;
}

bool SmuTree::insert(Node *newNode, std::span<std::byte> key)
{
	auto info = getParentInfo(key);

	if (!info)
		return false;

	newNode->keyData = key;
	newNode->parent = info->parent;
	newNode->left = nil;
	newNode->right = nil;
	newNode->color = Color::Red;

	if (info->parent == nil) {
		root = newNode;
	} else if (info->lastRes < 0) {
		info->parent->left = newNode;
	} else {
		info->parent->right = newNode;
	}

	stats.nodeCount.fetch_add(1, std::memory_order_relaxed);
	stats.keyDataSize.fetch_add(key.size_bytes(),
				    std::memory_order_relaxed);
	stats.treeSize.fetch_add(sizeof(Node), std::memory_order_relaxed);

	insertFixup(newNode);
	return true;
}

std::optional<SmuTree::ParentInfo>
SmuTree::getParentInfo(std::span<std::byte> key)
{
	Node *y = nil;
	Node *x = root;
	int res = 0;
	bool flag = true;

	while (x != nil) {
		y = x;
		res = compare(key, x->keyData);

		if (res == 0) {
			if (flag) {
				collision(x, key);
				x = root;
				flag = false;
				continue;
			}
			return std::nullopt;
		}

		x = (res < 0) ? x->left : x->right;
	}

	return ParentInfo{ y, res };
}

void SmuTree::rotateLeft(Node *x)
{
	Node *y = x->right;
	x->right = y->left;

	if (y->left != nil) {
		y->left->parent = x;
	}

	y->parent = x->parent;

	if (x->parent == nil) {
		root = y;
	} else if (x == x->parent->left) {
		x->parent->left = y;
	} else {
		x->parent->right = y;
	}

	y->left = x;
	x->parent = y;
}

void SmuTree::rotateRight(Node *y)
{
	Node *x = y->left;
	y->left = x->right;

	if (x->right != nil) {
		x->right->parent = y;
	}

	x->parent = y->parent;

	if (y->parent == nil) {
		root = x;
	} else if (y == y->parent->right) {
		y->parent->right = x;
	} else {
		y->parent->left = x;
	}

	x->right = y;
	y->parent = x;
}

void SmuTree::insertFixup(Node *z)
{
	while (z->parent->color == Color::Red) {
		fixupStep(z, z->parent == z->parent->parent->left);
	}
	root->color = Color::Black;
}

void SmuTree::fixupStep(Node *&z, bool isLeft)
{
	Node *uncle = isLeft ? z->parent->parent->right :
			       z->parent->parent->left;

	if (uncle->color == Color::Red) {
		z->parent->color = Color::Black;
		uncle->color = Color::Black;
		z->parent->parent->color = Color::Red;
		z = z->parent->parent;
	} else {
		if (z == (isLeft ? z->parent->right : z->parent->left)) {
			z = z->parent;
			isLeft ? rotateLeft(z) : rotateRight(z);
		}

		z->parent->color = Color::Black;
		z->parent->parent->color = Color::Red;
		isLeft ? rotateRight(z->parent->parent) :
			 rotateLeft(z->parent->parent);
	}
}

SmuTree::Node *SmuTree::minimum(Node *x)
{
	while (x->left != nil)
		x = x->left;

	return x;
}

void SmuTree::transplant(Node *u, Node *v)
{
	if (u->parent == nil) {
		root = v;
	} else if (u == u->parent->left) {
		u->parent->left = v;
	} else {
		u->parent->right = v;
	}
	v->parent = u->parent;
}

void SmuTree::removeFixup(Node *x, Node *parent)
{
	while (x != root && x->color == Color::Black) {
		bool isLeft = (x == parent->left);
		Node *sibling = isLeft ? parent->right : parent->left;

		if (sibling->color == Color::Red) {
			sibling->color = Color::Black;
			parent->color = Color::Red;
			isLeft ? rotateLeft(parent) : rotateRight(parent);
			sibling = isLeft ? parent->right : parent->left;
		}

		if (sibling->left->color == Color::Black &&
		    sibling->right->color == Color::Black) {
			sibling->color = Color::Red;
			x = parent;
			parent = x->parent;
		} else {
			if ((isLeft ? sibling->right->color :
				      sibling->left->color) == Color::Black) {
				(isLeft ? sibling->left : sibling->right)
					->color = Color::Black;
				sibling->color = Color::Red;
				isLeft ? rotateRight(sibling) :
					 rotateLeft(sibling);
				sibling = isLeft ? parent->right : parent->left;
			}

			sibling->color = parent->color;
			parent->color = Color::Black;
			(isLeft ? sibling->right : sibling->left)->color =
				Color::Black;
			isLeft ? rotateLeft(parent) : rotateRight(parent);
			x = root;
		}
	}
	x->color = Color::Black;
}

bool SmuTree::remove(Node *z)
{
	if (z == nullptr || z == nil || find(z->keyData) != z)
		return false;
	applyRemove(z);
	return true;
}

bool SmuTree::remove(std::span<std::byte> key)
{
	Node *z = find(key);
	if (!z)
		return false;
	applyRemove(z);
	return true;
}

void SmuTree::applyRemove(Node *z)
{
	Node *y = z;
	Node *x;
	Color y_original_color = y->color;
	size_t keySize = z->keyData.size_bytes();

	if (z->left == nil) {
		x = z->right;
		transplant(z, z->right);
	} else if (z->right == nil) {
		x = z->left;
		transplant(z, z->left);
	} else {
		y = minimum(z->right);
		y_original_color = y->color;
		x = y->right;
		if (y->parent == z) {
			x->parent = y;
		} else {
			transplant(y, y->right);
			y->right = z->right;
			y->right->parent = y;
		}
		transplant(z, y);
		y->left = z->left;
		y->left->parent = y;
		y->color = z->color;
	}

	stats.nodeCount.fetch_sub(1, std::memory_order_relaxed);
	stats.keyDataSize.fetch_sub(keySize, std::memory_order_relaxed);
	stats.treeSize.fetch_sub(sizeof(Node), std::memory_order_relaxed);

	if (y_original_color == Color::Black) {
		removeFixup(x, x->parent);
	}
}
