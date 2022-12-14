#pragma once
#include "Fixed_Array.h"
#include "hash.h"
#include "static_string.h"
#include "tuple.h"
#include "utility.h"
#include <cassert>
#include <ctre.hpp>
#include <variant>
#include <vector>
inline constexpr ignore_t placeholder;

constexpr size_t count_vars(const std::string_view _str) noexcept
{
	std::size_t count = 0;

	for (auto i : _str)
		if (i == '$')
			++count;

	return count;
}

template <std::size_t VarCount>
static inline constexpr std::size_t count_args = VarCount * 2 + 1;

template <std::size_t N = 0>
struct constant
{
	Fixed_String<N> str;

	constexpr constant(std::string_view view)
		: str(view)
	{}
};
struct string;

template <typename FixedStringView>
struct parsed_string
{
	using fixed_view = FixedStringView;

	bool is_var;
	constexpr parsed_string()
		: is_var(true), type(), name(), attribute() {}

	constexpr parsed_string(const fixed_view type, const fixed_view name, const fixed_view attribute)
		: is_var(true), type(type), name(name), attribute(attribute)
	{}

	constexpr parsed_string(fixed_view const_str)
		: is_var(false), attribute(const_str)
	{}

	constexpr auto get_constant() const { return attribute; }

	fixed_view type;
	fixed_view name;
	fixed_view attribute;
};

template <std::size_t N>
struct unspecified
{
	constexpr unspecified(std::size_t id_hash, std::string_view type, std::string_view fmt_str){};
};

template <>
struct unspecified<0>
{
	template <typename FixedStringView>
	constexpr unspecified(parsed_string<FixedStringView> str){};
};

template <typename Output, Fixed_String Type_Str = "">
struct variable_base
{
	std::size_t id_hash;
	constexpr variable_base() = default;
	constexpr variable_base(std::size_t id_hash)
		: id_hash(id_hash){};
	using type = Output;

	static constexpr bool str_match(const std::string_view str)
	{
		return Type_Str == str;
	}
};

template <typename Output>
struct variable;

template <>
struct variable<string> :
	variable_base<variable<string>, "str">
{
	using base = variable_base;
	using base::base;

	constexpr variable(std::size_t id_hash, std::string_view fmt_str)
		: base(id_hash)
	{}

	template <std::size_t N>
	constexpr auto operator()(Fixed_String<N> str) const
	{
		return constant<N - 1>(str);
	}
};

template <>
struct variable<int> :
	variable_base<variable<int>, "int">
{
	using base = variable_base<variable<int>, "int">;
	using base::base;

	constexpr variable(std::size_t id_hash, std::string_view fmt_str)
		: base(id_hash)
	{}
};

template <>
struct variable<float> :
	variable_base<variable<float>, "float">
{
	using base = variable_base;
	using base::base;

	constexpr variable(std::size_t id_hash, std::string_view fmt_str)
		: base(id_hash)
	{}
};

template <typename... Types>
using tuple_variable_wrapper = std::tuple<variable<Types>...>;

using basic_variables = tuple_variable_wrapper<string, int, float>;

static inline constexpr Fixed_String variable_regex = "\\$\\{(.+?)\\}";
static inline constexpr Fixed_String attribute_regex = "(.+):(.+(:.+)?)";

// TODO Make the assign lambdas skip over empty constants
template <typename FixedStringView, std::size_t VarCount>
struct parsed_string_array
{
	template <typename T>
	using array = Fixed_Array<T, count_args<VarCount>>;

	// using parsed_string_type = parsed_string<StrPtr>;
	using fixed_view = FixedStringView;
	using parsed_string_type = parsed_string<fixed_view>;
	using args_array = array<parsed_string_type>;

	using arg_iterator_const = typename args_array::const_iterator;

	args_array args;

	using value_type = typename args_array::value_type;
	using const_reference = typename args_array::const_reference;

	constexpr std::size_t size() const { return args.size(); }

	consteval parsed_string_array(const std::string_view str)
	{
		// auto make_view = [&str](std::size_t ptr, std::size_t size)
		// {
		// 	return fixed_view(ptr, size);
		// };

		auto assign_const = [](auto &itt, auto start, auto end)
		{
			fixed_view const_str(start, end - start);
			*itt = parsed_string_type(const_str);
			++itt;
		};

		auto assign_arg = [&str](auto &itt, auto var_str)
		{
			auto [match, type_match, name_match, attributes_match] = ctre::match<attribute_regex>(var_str);

			if (var_str.end() < str.data() + str.size())
			{
				if (!match)
					*itt = parsed_string_type(var_str);
				else
				{
					fixed_view type_view(type_match.to_view());
					fixed_view name_view(name_match.to_view());

					fixed_view attributes_view;
					if (attributes_match)
						attributes_view = fixed_view(attributes_match.to_view());

					*itt = parsed_string_type(type_view, name_view, attributes_view);
				}
			}
			++itt;
		};

		auto output_itt = args.begin();
		auto const_start = str.begin();

		for (auto [match, var] : ctre::range<variable_regex>(str))
		{
			assign_const(output_itt, const_start, match.begin());
			assign_arg(output_itt, var.to_view());

			const_start = match.end();
		}

		if (const_start < str.end())
			assign_const(output_itt, const_start, str.end());
	}

	template <typename ArrayT>
	constexpr auto iterate_args(auto action_func) const requires(std::invocable<decltype(action_func), typename ArrayT::reference, typename args_array::const_reference>)
	{
		ArrayT indices;

		for (auto arg = args.begin(), index = indices.begin(); arg < args.end(); arg++, index++)
			action_func(*index, *arg);

		return indices;
	}

	constexpr const_reference operator[](std::size_t Index) const { return args[Index]; }
};

namespace Argument
{

template <std::size_t ArgIndex = 0, std::size_t ParamIndex = 0>
struct container_iterator;

struct to_many_parameters;

template <typename T, std::size_t N = 0>
struct getter
{
	template <typename ContainerT>
	constexpr inline auto operator()(const ContainerT &container) const
	// requires(std::is_same_v<T, std::tuple_element_t<ContainerT, N>>)
	{
		return std::get<N>(container);
	}
};

template <typename ArgGetter, typename ParamGetter>
struct processor
{
	constexpr auto operator()(const auto &args, const auto &params) const
	{
		const auto &arg = ArgGetter()(args);
		const auto &param = ParamGetter()(params);

		return arg(param);
	}
};

template <typename ArgGetter>
struct processor<ArgGetter, ignore_t>
{
	constexpr auto operator()(const auto &args, const auto &) const { return ArgGetter()(args); }
};

// get_processor uses type deduction to determine which argument is being processed and whether the parameter should be consumed
// The default case is a processor that uses both argument and the parameter
template <typename Arg, typename Param, std::size_t ArgIndex, std::size_t ParamIndex>
struct get_processor
{
	using use_param = std::bool_constant<!std::is_same_v<ignore_t, Param>>;

	using arg_functor = getter<Arg, ArgIndex>;
	using param_functor = std::conditional_t<use_param::value, getter<Param, ParamIndex>, ignore_t>;

	using processor_type = processor<arg_functor, param_functor>;

	using next_iterator = container_iterator<ArgIndex + 1, ParamIndex + 1>;
};

// The argument is a constant so we just increment the arg Index
template <std::size_t N, typename Param, std::size_t ArgIndex, std::size_t ParamIndex>
struct get_processor<constant<N>, Param, ArgIndex, ParamIndex>
{
	using processor_type = processor<getter<constant<N>, ArgIndex>, ignore_t>;

	using next_iterator = container_iterator<ArgIndex + 1, ParamIndex>;
};

//
// Start of apply parameters
//
template <typename Arguments, typename Parameters, typename Results, typename ContainerIterator>
struct apply_parameters
{
	using type = Results;
};

template <typename Arguments, typename Parameters, typename Results, std::size_t ArgIndex, std::size_t ParamIndex>
	requires (std::tuple_size_v<Arguments> == ArgIndex) && (std::tuple_size_v<Parameters> > ParamIndex)
struct apply_parameters<Arguments, Parameters, Results, container_iterator<ArgIndex, ParamIndex>>
{
	using type = to_many_parameters;
};

template <typename Arguments, typename Parameters, typename Results, std::size_t ArgIndex, std::size_t ParamIndex>
	requires (std::tuple_size_v<Arguments> > ArgIndex) && (std::tuple_size_v<Parameters> > ParamIndex)
struct apply_parameters<Arguments, Parameters, Results, container_iterator<ArgIndex, ParamIndex>>
{
	using argument = std::tuple_element_t<ArgIndex, Arguments>;
	using parameter = std::tuple_element_t<ParamIndex, Parameters>;

	using result_data = get_processor<argument, parameter, ArgIndex, ParamIndex>;
	using result = typename result_data::processor_type;

	using results = concat_t<Results, result>;

  public:
	using type = typename apply_parameters<Arguments, Parameters, results, typename result_data::next_iterator>::type;
};

} // namespace Argument

template <typename Arguments, typename... Params>
using apply_argument_parameters = typename Argument::apply_parameters<Arguments, std::tuple<Params...>, type_sequence<>, Argument::container_iterator<0, 0>>::type;

template <typename... ArgsT>
struct format_args
{
	using arg_type = std::tuple<ArgsT...>;
	arg_type args;

	// constructor 1
	constexpr format_args(const ArgsT &...Args)
		: args{Args...}
	{}

	// constructor 2
	template <typename T, T... VariantIndex, T... Index>
	constexpr format_args(const indexable<T> auto ArgArray, integer_sequence<T, VariantIndex...>, integer_sequence<T, Index...>)
		: format_args(std::get<VariantIndex>(ArgArray[Index])...)
	{}

	// constructor 3
	template <typename T, T... VariantIndicies>
	constexpr format_args(const indexable<T> auto ArgArray, integer_sequence<T, VariantIndicies...> integer_seq)
		: format_args(ArgArray, integer_seq, std::make_index_sequence<ArgArray.size()>{})
	{}

	template <typename ParamIterator, typename... Getters>
	constexpr auto assign_vars_impl(const ParamIterator parameters, type_sequence<Getters...>) const
	{
		return ::format_args(Getters{}(args, parameters)...);
	}

	template <typename... ParamT>
	constexpr auto assign_vars(const ParamT... Params) const
	{
		using ResultArgs = apply_argument_parameters<arg_type, ParamT...>;

		return assign_vars_impl(std::tuple{Params...}, ResultArgs{});
	}
};

// TODO Fix this as it sorta works
template <typename FixedStringViewT, auto Arg, typename Head, typename... Tail>
static inline constexpr auto parse_variable()
{
	if constexpr (!Arg.is_var)
	{
		constexpr std::size_t constantSize = Arg.get_constant().size();

		return constant<constantSize>(Arg.get_constant());
	}
	else if constexpr (Head::str_match(Arg.type))
	{
		auto str_view = Arg.name;
		return Head(ELFHash(str_view.data(), str_view.size()), Arg.attribute);
	}
	// else if constexpr (Arg.type.empty())
	// {
	// 	auto str_view = make_str_view(Arg.name);
	// 	return unspecified<0>(ELFHash(str_view.data(), str_view.size()));
	// }
	else if constexpr (sizeof...(Tail) > 0)
	{
		return parse_variable<FixedStringViewT, Arg, Tail...>();
	}
	else
	{
		// constexpr std::size_t attributeSize = Arg.Attribute.size;

		return unspecified<0>(Arg);
	}
}

template <auto ArgViewArray, typename variables_list = basic_variables, typename Indicies = std::make_index_sequence<ArgViewArray.size()>>
struct arg_processor;

template <std::size_t N, typename FixedStringViewT, parsed_string_array<FixedStringViewT, N> ArgViewArray, typename... variables_list, typename IndexT, IndexT... Indicies>
struct arg_processor<ArgViewArray, std::tuple<variables_list...>, std::integer_sequence<IndexT, Indicies...>>
{
	constexpr auto operator()()
	{
		return format_args((parse_variable<FixedStringViewT, ArgViewArray[Indicies], variables_list...>())...);
	};
};

template <Fixed_String... SubStrs>
struct format_string
{
	static constexpr Fixed_String str{SubStrs...};

	static constexpr auto parsed_string_array_obj = parsed_string_array<fixed_string_view<&str.data>, count_vars(str)>(str);

	using parsed_string_array_processor = arg_processor<parsed_string_array_obj>;

	static constexpr auto args = parsed_string_array_processor()();
};

// Temporary
template <Fixed_String Str>
constexpr auto operator"" _fStr()
{
	return Str;
}

template <typename T>
struct formater
{};