/*
 * result.hpp
 *
 *  Created on: 11 июля 2015 г.
 *      Author: brysin
 */

#ifndef TIP_DB_PG_RESULT_HPP_
#define TIP_DB_PG_RESULT_HPP_

#include <tip/db/pg/common.hpp>
#include <iterator>
#include <istream>
#include <memory>
#include <tuple>

namespace tip {
namespace db {
namespace pg {

namespace detail {
struct result_impl;
}

/**
 * Result set.
 * Provide access to rows via indexing operators (random access)
 * and bidirectional access via iterators.
 * Access to field definitions.
 * @code
 * void
 * handle_results(connection_lock_ptr c, resultset res, bool complete)
 * {
 * 	if (complete) { // This is the last callback for the command
 * 		// C++11 style iteration
 * 		for (resultset::row const& row : res) {
 * 			for (resultset::field const& field : row) {
 * 			}
 * 		}
 *
 * 		// Oldschool iteration
 *		for (resultset::const_iterator row = res.begin(); row != res.end(); ++row) {
 *			// Do something with the row
 *			for (resultset::const_field_iterator f = row.begin(); f != row.end(); ++f) {
 *
 *			}
 *		}
 * 	}
 * }
 * @endcode
 */
class resultset {
public:
	typedef uint32_t	size_type;
	typedef int32_t difference_type;

	class row;
	class field;
	class const_row_iterator;

	typedef std::shared_ptr<detail::result_impl> result_impl_ptr;

	//@{
	/** @name Row container concept */
	typedef const_row_iterator const_iterator;
	typedef std::reverse_iterator< const_iterator > const_reverse_iterator;

	typedef row value_type;
	typedef const value_type reference;
	typedef const_iterator pointer;
	//@}

	//@{
	/** @name Field iterators */
	class const_field_iterator;
	typedef std::reverse_iterator<const_field_iterator> const_reverse_field_iterator;
	//@}
	typedef resultset const* result_pointer;

	static const size_type npos;
public:
	resultset();
	resultset(result_impl_ptr);
	//@{
	/** @name Row-wise container interface */
	size_type
	size() const; /**< Number of rows */
	bool
	empty() const; /**< Is the result set empty */

	const_iterator
	begin() const; /**< Iterator to the beginning of rows sequence */
	const_iterator
	end() const; /**< Iterator past the end of rows sequence. */

	const_reverse_iterator
	rbegin() const; /**< Iterator to the beginning of rows sequence in reverse order */
	const_reverse_iterator
	rend() const; /**< Iterator past the end of of rows sequence in reverse order */

	/**
	 * Get the first row in the result set.
	 * Will raise an assertion if the result set is empty.
	 * @return
	 */
	reference
	front() const;
	/**
	 * Get the last row in the result set.
	 * Will raise an assertion if the result set is empty.
	 * @return
	 */
	reference
	back() const;

	/**
	 * Access a row by index.
	 * In case of index out-of-range situation will rase an assertion
	 * @param index index of the row
	 * @return @c row object
	 */
	reference
	operator[](size_type index) const;
	/**
	 * Assess a row by index (range checking)
	 * In case of index out-of-range will throw an exception
	 * @param index index of the row
	 * @return @c row object
	 */
	reference
	at(size_type index) const;
	//@}

	//@{
	/** @name Result checking */
	/**
	 * Syntactic sugar operator for checking the result set
	 * @code
	 * void result_callback(resultset r)
	 * {
	 * 		if (r) {
	 * 			// Do something with the result
	 * 		}
	 * }
	 * @endcode
	 */
	operator bool() const
	{
		return !empty();
	}
	/**
	 * Syntactic sugar operator for checking the result set
	 * @code
	 * void result_callback(resultset r)
	 * {
	 * 		if (!r) {
	 * 			// Handle the situation of an empty result set
	 * 		}
	 * }
	 * @endcode
	 */
	bool
	operator!() const
	{
		return *this;
	}
	//@}
public:
	//@{
	/** @name Column-related interface */
	size_type
	columns_size() const; /**< Column count */

	/**
	 * Get the index of field with name
	 * @param name the field name
	 * @return if found, index in the range of [0..columns_size). If not found - npos
	 */
	size_type
	index_of_name(std::string const& name) const;

	/**
	 * Get the field description of field by it's index.
	 * @param col_index field index, must be in range of [0..columns_size)
	 * @return constant reference to the field description
	 * @throws out_of_range exception
	 */
	field_description const&
	field(size_type col_index) const;

	/**
	 * Get the field description of field by it's name.
	 * @param name name of the field. must be present in the result set.
	 * @return constant reference to the field description
	 * @throws out_of_range exception
	 */
	field_description const&
	field(std::string const& name) const;

	/**
	 * Get the name of field by it's index
	 * @param col_index field index, must be in range of [0..columns_size)
	 * @return the name of the field.
	 * @throws out_of_range exception
	 */
	std::string const&
	field_name(size_type col_index) const;
	//@}
public:
	//@{
	/** @name Data access classes */
	/**
	 * Represents a data row in the result set.
	 * Doesn't hold any data except a pointer to the result set
	 * and the data row index. Must not outlive the parent result set.
	 * Row is not constructible outside of the result set;
	 */
	class row {
	public:
		typedef uint16_t 					size_type;
		typedef resultset::difference_type	difference_type;

		//@{
		/** @name Field container concept */
		typedef const_field_iterator const_iterator;
		typedef const_reverse_field_iterator const_reverse_iterator;

		typedef class field value_type;
		typedef class field reference;
		typedef const_iterator pointer;
		//@}
		static const size_type npos;
	public:
		size_type
		row_index() const /**< Index of the row in the result set */
		{ return row_index_; }

		//@{
		/** @name Field container interface */
		size_type
		size() const; /**< Number of fields */
		/**
		 * Is row empty.
		 * Actually, shouln'd happen in a non-empty result set.
		 * @return
		 */
		bool
		empty() const;

		const_iterator
		begin() const; /**< Iterator to the beginning of field sequence */
		const_iterator
		end() const; /**< Iterator past the field sequence */

		const_reverse_iterator
		rbegin() const;
		const_reverse_iterator
		rend() const;
		//@}
		//@{
		/** @name Field access */
		/**
		 * Get field by it's index.
		 * @param index
		 * @return
		 */
		reference
		operator[](size_type index) const;
		/**
		 * Get field by it's name
		 * @param name
		 * @return
		 */
		reference
		operator[](std::string const& name) const;
		//@}

		/**
		 * Get the row as a tuple of typed values
		 * @param target tuple
		 */
		template < typename ... T >
		void
		to( std::tuple< T ... >& ) const;

		template < typename ... T >
		void
		to( std::tuple< T& ... > ) const;
	protected:
		friend class resultset;
		/**
		 * Construct a row object in a result set.
		 * @param res
		 * @param idx
		 */
		row(result_pointer res, resultset::size_type idx)
			: result_(res), row_index_(idx) {}

		result_pointer 			result_;
		resultset::size_type	row_index_;
	}; // row
	/**
	 * Represents a single field in the result set.
	 * Provides access to underlying data and functions for
	 * converting the data into various datatypes.
	 */
	class field {
	public:
		size_type
		row_index() const /**< @brief Index of owner row */
		{ return row_index_; }
		row::size_type
		field_index() const /**< @brief Index of field in the row */
		{ return field_index_; }

		std::string const&
		name() const; /**< @brief Name of field */

		bool
		is_null() const; /**< @brief Is field value null */

		bool
		empty() const; /**< @brief Is field value empty (not null) */

		template < typename T >
		bool
		to( T& val ) const
		{
			field_buffer b = input_buffer();
			std::istream in(&b);
			if (in >> val)
				return true;
			return false;
		}

		template < typename T >
		typename std::decay<T>::type
		as() const
		{
			typename std::decay<T>::type val;
			to(val);
			return val;
		}
	private:
		field_buffer
		input_buffer() const;
	protected:
		friend class resultset;
		friend class row;
		field(result_pointer res, size_type row, row::size_type col) :
			result_(res), row_index_(row), field_index_(col) {}
		result_pointer 	result_;
		size_type		row_index_;
		row::size_type	field_index_;
	}; // field
	//@}
	//@{
	/** @name Data iterators */
	/**
	 * Iterator over the rows in a result set
	 */
	class const_row_iterator : public row {
	public:
		//@{
		/** Iterator concept */
		typedef row value_type;
		typedef resultset::difference_type difference_type;
		typedef value_type reference;
		typedef value_type const* pointer;
		typedef std::random_access_iterator_tag iterator_category;
		//@}
	public:
		/**
		 * Create a terminating iterator
		 */
		const_row_iterator() : row(nullptr, npos) {}

		//@{
		/** @name Dereferencing */
		reference
		operator*() const
		{
			return reference(*this);
		}
		pointer
		operator->() const
		{
			return this;
		}
		//@}
		//@{
		/** @name Iterator validity checking */
		/**
		 * Implicitly convert to boolean.
		 * The row index is valid when it is equal to the result set size
		 * for an iterator that is returned by end() function to be valid
		 * and moveable.
		 */
		operator bool() const
		{ return result_ && row_index_ <= result_->size(); }
		bool
		operator !() const
		{ return !(this->operator bool()); }
		//@}
		//@{
		/** @name Random access iterator concept */
		const_row_iterator&
		operator++() /**< Prefix increment */
		{ return advance(1); }

		const_row_iterator
		operator++(int) /**< Postfix increment */
		{
			const_row_iterator i(*this);
			advance(1);
			return i;
		}
		const_row_iterator&
		operator--() /**< Prefix decrement */
		{ return advance(-1); }

		const_row_iterator
		operator--(int) /**< Postfix decrement */
		{
			const_row_iterator i(*this);
			advance(-1);
			return i;
		}

		const_row_iterator&
		operator += (difference_type distance)
		{ return advance(distance); }
		const_row_iterator&
		operator -= (difference_type distance)
		{ return advance(-distance); }
		//@}

		//@{
		/** @name Iterator comparison */
		bool
		operator == (const_row_iterator const& rhs) const
		{ return compare(rhs) == 0; }

		bool
		operator != (const_row_iterator const& rhs) const
		{ return !(*this == rhs); }

		bool
		operator < (const_row_iterator const& rhs) const
		{ return compare(rhs) < 0; }
		bool
		operator <= (const_row_iterator const& rhs) const
		{ return compare(rhs) <= 0; }
		bool
		operator > (const_row_iterator const& rhs) const
		{ return compare(rhs) > 0; }
		bool
		operator >= (const_row_iterator const& rhs) const
		{ return compare(rhs) >= 0; }
		//@}

	private:
		friend class resultset;
		const_row_iterator(result_pointer res, resultset::size_type index)
			: row(res, index) {}
		int
		compare(const_row_iterator const& rhs) const;
		const_row_iterator&
		advance(difference_type);
	}; // const_row_iterator

	/**
	 * Iterator over the fields in a data row
	 */
	class const_field_iterator : public field {
	public:
		typedef field value_type;
		typedef resultset::difference_type difference_type;
		typedef value_type reference;
		typedef value_type const* pointer;
		typedef std::random_access_iterator_tag iterator_category;
	public:
		/**
		 * Create a terminating iterator
		 */
		const_field_iterator() : field(nullptr, npos, npos) {}

		//@{
		/** @name Dereferencing */
		reference
		operator*() const
		{
			return reference(*this);
		}
		pointer
		operator->() const
		{
			return this;
		}
		//@}
		//@{
		/** @name Iterator validity checking */
		/**
		 * Implicitly convert to boolean.
		 * The row index is valid when it is equal to the row size
		 * for an iterator that is returned by end() function to be valid
		 * and moveable.
		 */
		operator bool() const
		{ return result_
				&& row_index_ < result_->size()
				&& field_index_ <= result_->columns_size(); }
		bool
		operator !() const
		{ return !(this->operator bool()); }
		//@}
		//@{
		/** @name Random access iterator concept */
		const_field_iterator&
		operator++() /**< Prefix increment */
		{ return advance(1); }

		const_field_iterator
		operator++(int) /**< Postfix increment */
		{
			const_field_iterator i(*this);
			advance(1);
			return i;
		}
		const_field_iterator&
		operator--() /**< Prefix decrement */
		{ return advance(-1); }

		const_field_iterator
		operator--(int) /**< Postfix decrement */
		{
			const_field_iterator i(*this);
			advance(-1);
			return i;
		}

		const_field_iterator&
		operator += (difference_type distance)
		{ return advance(distance); }
		const_field_iterator&
		operator -= (difference_type distance)
		{ return advance(-distance); }
		//@}

		//@{
		/** @name Iterator comparison */
		bool
		operator == (const_field_iterator const& rhs) const
		{ return compare(rhs) == 0; }

		bool
		operator != (const_field_iterator const& rhs) const
		{ return !(*this == rhs); }

		bool
		operator < (const_field_iterator const& rhs) const
		{ return compare(rhs) < 0; }
		bool
		operator <= (const_field_iterator const& rhs) const
		{ return compare(rhs) <= 0; }
		bool
		operator > (const_field_iterator const& rhs) const
		{ return compare(rhs) > 0; }
		bool
		operator >= (const_field_iterator const& rhs) const
		{ return compare(rhs) >= 0; }
		//@}
	private:
		friend class row;
		const_field_iterator(result_pointer res, size_type row, size_type col)
			: field(res, row, col) {}
		int
		compare(const_field_iterator const& rhs) const;
		const_field_iterator&
		advance(difference_type distance);
	}; // const_field_iterator
	//@}
private:
	friend class row;
	friend class field;
	typedef std::shared_ptr<const detail::result_impl> const_result_impl_ptr;
	const_result_impl_ptr pimpl_;

	field_buffer
	at(size_type r, row::size_type c) const;

	bool
	is_null(size_type r, row::size_type c) const;
}; // resultset

inline resultset::row::difference_type
operator - (resultset::row const& a, resultset::row const& b)
{
	return a.row_index() - b.row_index();
}

/**
 * Template member specialization for booleans
 * @param
 */
template < >
bool
resultset::field::to(bool&) const;

/**
 * Template member specialization for booleans
 * @param
 */
template < >
bool
resultset::field::to(std::string&) const;

}  // namespace pg
}  // namespace db
}  // namespace tip



#endif /* TIP_DB_PG_RESULT_HPP_ */