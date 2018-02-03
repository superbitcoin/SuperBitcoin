#pragma once

template<typename C>
class ContainerIterator
{
public:
    ContainerIterator(C& c) : _c(c), _i(c.begin()) {}

    bool IsEnd() const
    {
        return _i == _c.end();
    }

    typename C::reference Get()
    {
        return *_i;
    }

    typename C::pointer operator->()
    {
        return _i.operator->();
    }

    ContainerIterator& operator++()
    {
        ++_i;
        return *this;
    }

    ContainerIterator operator++(int)
    {
        _i++;
        return *this;
    }

    void Next()
    {
        ++_i;
    }

private:
    C& _c;
    typename C::iterator _i;
};


template<typename C>
class ReverseContainerIterator
{
public:
    ReverseContainerIterator(C& c) : _c(c), _i(c.rbegin()) {}

    bool IsEnd() const
    {
        return _i == _c.rend();
    }

    typename C::reference Get()
    {
        return *_i;
    }

    typename C::pointer operator->()
    {
        return _i.operator->();
    }

    ReverseContainerIterator& operator++()
    {
        ++_i;
        return *this;
    }

    ReverseContainerIterator operator++(int)
    {
        _i++;
        return *this;
    }

    void Next()
    {
        ++_i;
    }

private:
    C& _c;
    typename C::reverse_iterator _i;
};

template<template<typename> class CI, typename C>
inline CI<C> MakeContainerIterator(C& c)
{
    return CI<C>(c);
};

template<typename C>
inline ContainerIterator<C> MakeForwardContainerIterator(C& c)
{
    return ContainerIterator<C>(c);
}

template<typename C>
inline ReverseContainerIterator<C> MakeReverseContainerIterator(C& c)
{
    return ReverseContainerIterator<C>(c);
}

