#include "smu_tree.h"
#include <algorithm>

SmuTree::Node::Node()
	: color(Color::RED)
	, left(NIL)
	, right(NIL)
	, parent(NIL)
{
}

std::span<std::byte> SmuTree::Iterator::operator*() const
{
	return tree->getData(current);
}
bool SmuTree::Iterator::operator!=(const Iterator &other) const
{
	return current != other.current;
}
bool SmuTree::Iterator::operator==(const Iterator &other) const
{
	return current == other.current;
}
SmuTree::Iterator &SmuTree::Iterator::operator++()
{
	current = tree->successor(current);
	return *this;
}

SmuTree::SmuTree(std::span<Node> mem, CompareFunc comp)
	: storage(mem)
	, compare(comp)
{
	if (!storage.empty()) {
		Node &nil = get(Node::NIL);
		nil.color = Color::BLACK;
		nil.left = nil.right = nil.parent = Node::NIL;
	}
}

SmuTree::Node &SmuTree::get(uintptr_t idx)
{
	return storage[idx];
}
const SmuTree::Node &SmuTree::get(uintptr_t idx) const
{
	return storage[idx];
}

std::span<std::byte> SmuTree::getData(uintptr_t idx) const
{
	return (idx == Node::NIL) ? std::span<std::byte>{} : storage[idx].data;
}

SmuTree::Iterator SmuTree::begin() const
{
	return { this, minimum(root) };
}
SmuTree::Iterator SmuTree::end() const
{
	return { this, Node::NIL };
}
bool SmuTree::empty() const
{
	return root == Node::NIL;
}

uintptr_t SmuTree::minimum(uintptr_t n) const
{
	if (n == Node::NIL)
		return Node::NIL;
	while (get(n).left != Node::NIL) {
		n = get(n).left;
	}
	return n;
}

uintptr_t SmuTree::successor(uintptr_t x) const
{
	if (x == Node::NIL)
		return Node::NIL;
	if (get(x).right != Node::NIL) {
		return minimum(get(x).right);
	}

	uintptr_t y = get(x).parent;
	while (y != Node::NIL && x == get(y).right) {
		x = y;
		y = get(y).parent;
	}
	return y;
}

void SmuTree::transplant(uintptr_t u, uintptr_t v)
{
	if (get(u).parent == Node::NIL) {
		root = v;
	} else if (u == get(get(u).parent).left) {
		get(get(u).parent).left = v;
	} else {
		get(get(u).parent).right = v;
	}
	get(v).parent = get(u).parent;
}

uintptr_t SmuTree::find(std::span<std::byte> data) const
{
	uintptr_t curr = root;
	while (curr != Node::NIL) {
		int res = compare(data, get(curr).data);
		if (res == 0)
			return curr;
		curr = (res < 0) ? get(curr).left : get(curr).right;
	}
	return Node::NIL;
}

bool SmuTree::insert(std::span<std::byte> data)
{
	uintptr_t z;
	if (free_head != Node::NIL) {
		z = free_head;
		free_head = get(z).left;
	} else {
		if (next_free >= storage.size())
			return false;
		z = next_free++;
	}

	Node &node = get(z);
	node.data = data;
	node.left = node.right = node.parent = Node::NIL;
	node.color = Color::RED;

	uintptr_t y = Node::NIL;
	uintptr_t x = root;
	while (x != Node::NIL) {
		y = x;
		if (compare(node.data, get(x).data) < 0)
			x = get(x).left;
		else
			x = get(x).right;
	}

	node.parent = y;
	if (y == Node::NIL)
		root = z;
	else if (compare(node.data, get(y).data) < 0)
		get(y).left = z;
	else
		get(y).right = z;

	fixViolation(z);
	return true;
}

bool SmuTree::remove(std::span<std::byte> data)
{
	uintptr_t z = find(data);
	if (z == Node::NIL)
		return false;

	uintptr_t node_to_free = z;
	uintptr_t y = z;
	uintptr_t x;
	Color y_original_color = get(y).color;

	if (get(z).left == Node::NIL) {
		x = get(z).right;
		transplant(z, get(z).right);
	} else if (get(z).right == Node::NIL) {
		x = get(z).left;
		transplant(z, get(z).left);
	} else {
		y = minimum(get(z).right);
		y_original_color = get(y).color;
		x = get(y).right;
		if (get(y).parent == z) {
			get(x).parent = y;
		} else {
			transplant(y, get(y).right);
			get(y).right = get(z).right;
			get(get(y).right).parent = y;
		}
		transplant(z, y);
		get(y).left = get(z).left;
		get(get(y).left).parent = y;
		get(y).color = get(z).color;
	}

	if (y_original_color == Color::BLACK) {
		fixDeletion(x);
	}

	get(node_to_free).left = free_head;
	free_head = node_to_free;
	return true;
}

void SmuTree::rotateLeft(uintptr_t x)
{
	uintptr_t y = get(x).right;
	get(x).right = get(y).left;
	if (get(y).left != Node::NIL)
		get(get(y).left).parent = x;
	get(y).parent = get(x).parent;
	if (get(x).parent == Node::NIL)
		root = y;
	else if (x == get(get(x).parent).left)
		get(get(x).parent).left = y;
	else
		get(get(x).parent).right = y;
	get(y).left = x;
	get(x).parent = y;
}

void SmuTree::rotateRight(uintptr_t y)
{
	uintptr_t x = get(y).left;
	get(y).left = get(x).right;
	if (get(x).right != Node::NIL)
		get(get(x).right).parent = y;
	get(x).parent = get(y).parent;
	if (get(y).parent == Node::NIL)
		root = x;
	else if (y == get(get(y).parent).left)
		get(get(y).parent).left = x;
	else
		get(get(y).parent).right = x;
	get(x).right = y;
	get(y).parent = x;
}

void SmuTree::fixViolation(uintptr_t z)
{
	while (z != root && get(get(z).parent).color == Color::RED) {
		uintptr_t p = get(z).parent;
		uintptr_t g = get(p).parent;
		if (p == get(g).left) {
			uintptr_t u = get(g).right;
			if (get(u).color == Color::RED) {
				get(g).color = Color::RED;
				get(p).color = Color::BLACK;
				get(u).color = Color::BLACK;
				z = g;
			} else {
				if (z == get(p).right) {
					z = p;
					rotateLeft(z);
					p = get(z).parent;
					g = get(p).parent;
				}
				get(p).color = Color::BLACK;
				get(g).color = Color::RED;
				rotateRight(g);
			}
		} else {
			uintptr_t u = get(g).left;
			if (get(u).color == Color::RED) {
				get(g).color = Color::RED;
				get(p).color = Color::BLACK;
				get(u).color = Color::BLACK;
				z = g;
			} else {
				if (z == get(p).left) {
					z = p;
					rotateRight(z);
					p = get(z).parent;
					g = get(p).parent;
				}
				get(p).color = Color::BLACK;
				get(g).color = Color::RED;
				rotateLeft(g);
			}
		}
	}
	get(root).color = Color::BLACK;
}

void SmuTree::fixDeletion(uintptr_t x)
{
	while (x != root && get(x).color == Color::BLACK) {
		uintptr_t xp = get(x).parent;
		if (x == get(xp).left) {
			uintptr_t w = get(xp).right;
			if (get(w).color == Color::RED) {
				get(w).color = Color::BLACK;
				get(xp).color = Color::RED;
				rotateLeft(xp);
				w = get(get(x).parent).right;
			}
			if (get(get(w).left).color == Color::BLACK &&
			    get(get(w).right).color == Color::BLACK) {
				get(w).color = Color::RED;
				x = get(x).parent;
			} else {
				if (get(get(w).right).color == Color::BLACK) {
					get(get(w).left).color = Color::BLACK;
					get(w).color = Color::RED;
					rotateRight(w);
					w = get(get(x).parent).right;
				}
				get(w).color = get(get(x).parent).color;
				get(get(x).parent).color = Color::BLACK;
				get(get(w).right).color = Color::BLACK;
				rotateLeft(get(x).parent);
				x = root;
			}
		} else {
			uintptr_t w = get(xp).left;
			if (get(w).color == Color::RED) {
				get(w).color = Color::BLACK;
				get(xp).color = Color::RED;
				rotateRight(xp);
				w = get(get(x).parent).left;
			}
			if (get(get(w).right).color == Color::BLACK &&
			    get(get(w).left).color == Color::BLACK) {
				get(w).color = Color::RED;
				x = get(x).parent;
			} else {
				if (get(get(w).left).color == Color::BLACK) {
					get(get(w).right).color = Color::BLACK;
					get(w).color = Color::RED;
					rotateLeft(w);
					w = get(get(x).parent).left;
				}
				get(w).color = get(get(x).parent).color;
				get(get(x).parent).color = Color::BLACK;
				get(get(w).left).color = Color::BLACK;
				rotateRight(get(x).parent);
				x = root;
			}
		}
	}
	get(x).color = Color::BLACK;
}

size_t SmuTree::size() const
{
	size_t count = 0;

	for (auto it = begin(); it != end(); ++it) {
		count++;
	}
	return count;
}

size_t SmuTree::memory_usage() const
{
	return sizeof(*this) + storage.size_bytes();
}
