#ifndef __algoplus_hpp__
#define __algoplus_hpp__

//Performs f() on any element elem in [first, last) where pred(elem) is true
template<typename InputIterator, typename Predicate, typename UnaryFunction>
UnaryFunction for_each_if(InputIterator first, InputIterator last, 
						  Predicate pred, UnaryFunction f)
{
  for(;first != last; ++first)
  {
	if (pred(*first))
	{
	  f(*first);
	}
  }
  return f;
}

// -----------------------------------------------------------------------

template <typename InputIterator, typename OutputIterator, typename Predicate>
OutputIterator copy_if(InputIterator first, InputIterator last, 
					   OutputIterator dest, Predicate p)
{
  for (; first != last; ++first)
  {
	if (p(*first))
	{
	  *dest = *first;
	  ++dest;
	}
  }
  return dest;
}

#endif