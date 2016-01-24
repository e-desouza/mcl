#pragma once
/**
	@file
	@brief finite field class
	@author MITSUNARI Shigeo(@herumi)
	@license modified new BSD license
	http://opensource.org/licenses/BSD-3-Clause
*/
#include <iostream>
#include <sstream>
#include <vector>
#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable : 4127)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef MCL_NO_AUTOLINK
		#ifdef NDEBUG
			#pragma comment(lib, "mcl.lib")
		#else
			#pragma comment(lib, "mcld.lib")
		#endif
	#endif
#endif
#include <cybozu/hash.hpp>
#include <mcl/op.hpp>
#include <mcl/util.hpp>

namespace mcl {

namespace fp {

struct TagDefault;

void arrayToStr(std::string& str, const Unit *x, size_t n, int base, bool withPrefix);

/*
	set x and y[] with abs(str)
	pBitSize is set if not null
	return true if str is negative
*/
bool strToMpzArray(size_t *pBitSize, Unit *y, size_t maxBitSize, mpz_class& x, const std::string& str, int base);

void copyAndMask(Unit *y, const void *x, size_t xByteSize, const Op& op, bool doMask);

uint64_t getUint64(bool *pb, const fp::Block& b);
int64_t getInt64(bool *pb, fp::Block& b, const fp::Op& op);

} // mcl::fp

template<class tag = fp::TagDefault, size_t maxBitSize = MCL_MAX_OP_BIT_SIZE>
class FpT {
	typedef fp::Unit Unit;
	static const size_t maxSize = (maxBitSize + fp::UnitBitSize - 1) / fp::UnitBitSize;
	static fp::Op op_;
	template<class tag2, size_t maxBitSize2> friend class FpT;
	Unit v_[maxSize];
public:
	// return pointer to array v_[]
	const Unit *getUnit() const { return v_; }
	static inline size_t getUnitSize() { return op_.N; }
	static inline size_t getBitSize() { return op_.bitSize; }
	void dump() const
	{
		const size_t N = op_.N;
		for (size_t i = 0; i < N; i++) {
			printf("%016llx ", (long long)v_[N - 1 - i]);
		}
		printf("\n");
	}
	static inline void setModulo(const std::string& mstr, int base = 0, fp::Mode mode = fp::FP_AUTO)
	{
		assert(maxBitSize <= MCL_MAX_OP_BIT_SIZE);
		assert(sizeof(mp_limb_t) == sizeof(Unit));
		// set default wrapper function
		op_.neg = negW;
		op_.sqr = sqrW;
		op_.add = addW;
		op_.sub = subW;
		op_.mul = mulW;
/*
	priority : MCL_USE_XBYAK > MCL_USE_LLVM > none
	Xbyak > llvm_opt > llvm > gmp
*/
#ifdef MCL_USE_XBYAK
		if (mode == fp::FP_AUTO) mode = fp::FP_XBYAK;
#else
		if (mode == fp::FP_XBYAK) mode = fp::FP_AUTO;
#endif
#ifdef MCL_USE_LLVM
		if (mode == fp::FP_AUTO) mode = fp::FP_LLVM_MONT;
#else
		if (mode == fp::FP_LLVM || mode == fp::FP_LLVM_MONT) mode = fp::FP_AUTO;
#endif
		if (mode == fp::FP_AUTO) mode = fp::FP_GMP;

		op_.useMont = mode == fp::FP_LLVM_MONT || mode == fp::FP_XBYAK;
		if (mode == fp::FP_LLVM_MONT) {
			op_.mul = montW;
			op_.sqr = montSqrW;
		}
#if 0
	fprintf(stderr, "mode=%d, useMont=%d"
#ifdef MCL_USE_XBYAK
		" ,MCL_USE_XBYAK"
#endif
#ifdef MCL_USE_LLVM
		" ,MCL_USE_LLVM"
#endif
	"\n", mode, op_.useMont);
#endif
		op_.init(mstr, base, maxBitSize, mode);
		{ // set oneRep
			FpT x;
			x.clear();
			x.v_[0] = 1;
			op_.toMont(x.v_, x.v_);
			op_.copy(op_.oneRep, x.v_);
		}
		{ // set half
			mpz_class half = (op_.mp - 1) / 2;
			Gmp::getArray(op_.half, op_.N, half);
		}
	}
	static inline void getModulo(std::string& pstr)
	{
		Gmp::getStr(pstr, op_.mp);
	}
	static inline bool isOdd(const FpT& x)
	{
		fp::Block b;
		x.getBlock(b);
		return (b.p[0] & 1) == 1;
	}
	static inline bool squareRoot(FpT& y, const FpT& x)
	{
		mpz_class mx, my;
		x.getMpz(mx);
		bool b = op_.sq.get(my, mx);
		if (!b) return false;
		y.setMpz(my);
		return true;
	}
	FpT() {}
	FpT(const FpT& x)
	{
		op_.copy(v_, x.v_);
	}
	FpT& operator=(const FpT& x)
	{
		op_.copy(v_, x.v_);
		return *this;
	}
	void clear()
	{
		op_.clear(v_);
	}
	FpT(int64_t x) { operator=(x); }
	explicit FpT(const std::string& str, int base = 0)
	{
		setStr(str, base);
	}
	FpT& operator=(int64_t x)
	{
		clear();
		if (x == 1) {
			op_.copy(v_, op_.oneRep);
		} else if (x) {
			int64_t y = x < 0 ? -x : x;
			if (sizeof(Unit) == 8) {
				v_[0] = y;
			} else {
				v_[0] = (uint32_t)y;
				v_[1] = (uint32_t)(y >> 32);
			}
			if (x < 0) neg(*this, *this);
			toMont(*this, *this);
		}
		return *this;
	}
	static inline bool useMont() { return op_.useMont; }
	void toMont(FpT& y, const FpT& x)
	{
		if (useMont()) op_.toMont(y.v_, x.v_);
	}
	void fromMont(FpT& y, const FpT& x)
	{
		if (useMont()) op_.fromMont(y.v_, x.v_);
	}
	void setStr(const std::string& str, int base = 0)
	{
		mpz_class x;
		bool isMinus = fp::strToMpzArray(0, v_, op_.N * fp::UnitBitSize, x, str, base);
		if (x >= op_.mp) throw cybozu::Exception("FpT:setStr:large str") << str << op_.mp;
		if (isMinus) {
			neg(*this, *this);
		}
		toMont(*this, *this);
	}
	/*
		throw exception if x >= p
	*/
	template<class S>
	void setArray(const S *x, size_t n)
	{
		fp::copyAndMask(v_, x, sizeof(S) * n, op_, false);
		toMont(*this, *this);
	}
	/*
		mask inBuf with (1 << (bitLen - 1)) - 1
	*/
	template<class S>
	void setArrayMask(const S *inBuf, size_t n)
	{
		fp::copyAndMask(v_, inBuf, sizeof(S) * n, op_, true);
		toMont(*this, *this);
	}
	template<class S>
	size_t getArray(S *outBuf, size_t n) const
	{
		const size_t SbyteSize = sizeof(S) * n;
		const size_t fpByteSize = sizeof(Unit) * op_.N;
		if (SbyteSize < fpByteSize) throw cybozu::Exception("FpT:getArray:bad n") << n << fpByteSize;
		assert(SbyteSize >= fpByteSize);
		fp::Block b;
		getBlock(b);
		memcpy(outBuf, b.p, fpByteSize);
		const size_t writeN = (fpByteSize + sizeof(S) - 1) / sizeof(S);
		memset((char *)outBuf + fpByteSize, 0, writeN * sizeof(S) - fpByteSize);
		return writeN;
	}
	void getBlock(fp::Block& b) const
	{
		b.n = op_.N;
		if (useMont()) {
			op_.fromMont(b.v_, v_);
			b.p = &b.v_[0];
		} else {
			b.p = &v_[0];
		}
	}
	template<class RG>
	void setRand(RG& rg)
	{
		fp::getRandVal(v_, rg, op_.p, op_.bitSize);
		toMont(*this, *this);
	}
	void getStr(std::string& str, int base = 10, bool withPrefix = false) const
	{
		fp::Block b;
		getBlock(b);
		fp::arrayToStr(str, b.p, b.n, base, withPrefix);
	}
	std::string getStr(int base = 10, bool withPrefix = false) const
	{
		std::string str;
		getStr(str, base, withPrefix);
		return str;
	}
	void getMpz(mpz_class& x) const
	{
		fp::Block b;
		getBlock(b);
		Gmp::setArray(x, b.p, b.n);
	}
	mpz_class getMpz() const
	{
		mpz_class x;
		getMpz(x);
		return x;
	}
	void setMpz(const mpz_class& x)
	{
		setArray(Gmp::getUnit(x), Gmp::getUnitSize(x));
	}
	static inline void add(FpT& z, const FpT& x, const FpT& y) { op_.add(z.v_, x.v_, y.v_); }
	static inline void sub(FpT& z, const FpT& x, const FpT& y) { op_.sub(z.v_, x.v_, y.v_); }
	static inline void mul(FpT& z, const FpT& x, const FpT& y) { op_.mul(z.v_, x.v_, y.v_); }
	static inline void inv(FpT& y, const FpT& x) { op_.invOp(y.v_, x.v_, op_); }
	static inline void neg(FpT& y, const FpT& x) { op_.neg(y.v_, x.v_); }
	static inline void square(FpT& y, const FpT& x) { op_.sqr(y.v_, x.v_); }
	static inline void div(FpT& z, const FpT& x, const FpT& y)
	{
		FpT rev;
		inv(rev, y);
		mul(z, x, rev);
	}
	static inline void powerArray(FpT& z, const FpT& x, const Unit *y, size_t yn, bool isNegative)
	{
		FpT tmp;
		const FpT *px = &x;
		if (&z == &x) {
			tmp = x;
			px = &tmp;
		}
		z = 1;
		fp::powerGeneric(z, *px, y, yn, FpT::mul, FpT::square);
		if (isNegative) {
			FpT::inv(z, z);
		}
	}
	template<class tag2, size_t maxBitSize2>
	static inline void power(FpT& z, const FpT& x, const FpT<tag2, maxBitSize2>& y)
	{
		fp::Block b;
		y.getBlock(b);
		powerArray(z, x, b.p, b.n, false);
	}
	static inline void power(FpT& z, const FpT& x, int y)
	{
		const Unit u = abs(y);
		powerArray(z, x, &u, 1, y < 0);
	}
	static inline void power(FpT& z, const FpT& x, const mpz_class& y)
	{
		powerArray(z, x, Gmp::getUnit(y), abs(y.get_mpz_t()->_mp_size), y < 0);
	}
	bool isZero() const { return op_.isZero(v_); }
	bool isOne() const { return fp::isEqualArray(v_, op_.oneRep, op_.N); }
	/*
		return true if p/2 < x < p
		return false if 0 <= x <= p/2
		note p/2 == (p-1)/2 because of p is odd
	*/
	bool isNegative() const
	{
		fp::Block b;
		getBlock(b);
		return fp::isGreaterArray(b.p, op_.half, op_.N);
	}
	bool isValid() const
	{
		return fp::isLessArray(v_, op_.p, op_.N);
	}
	uint64_t getUint64(bool *pb = 0) const
	{
		fp::Block b;
		getBlock(b);
		return fp::getUint64(pb, b);
	}
	int64_t getInt64(bool *pb = 0) const
	{
		fp::Block b;
		getBlock(b);
		return fp::getInt64(pb, b, op_);
	}
	static inline size_t getModBitLen() { return op_.bitSize; }
	bool operator==(const FpT& rhs) const { return fp::isEqualArray(v_, rhs.v_, op_.N); }
	bool operator!=(const FpT& rhs) const { return !operator==(rhs); }
	inline friend FpT operator+(const FpT& x, const FpT& y) { FpT z; add(z, x, y); return z; }
	inline friend FpT operator-(const FpT& x, const FpT& y) { FpT z; sub(z, x, y); return z; }
	inline friend FpT operator*(const FpT& x, const FpT& y) { FpT z; mul(z, x, y); return z; }
	inline friend FpT operator/(const FpT& x, const FpT& y) { FpT z; div(z, x, y); return z; }
	FpT& operator+=(const FpT& x) { add(*this, *this, x); return *this; }
	FpT& operator-=(const FpT& x) { sub(*this, *this, x); return *this; }
	FpT& operator*=(const FpT& x) { mul(*this, *this, x); return *this; }
	FpT& operator/=(const FpT& x) { div(*this, *this, x); return *this; }
	FpT operator-() const { FpT x; neg(x, *this); return x; }
	friend inline std::ostream& operator<<(std::ostream& os, const FpT& self)
	{
		const std::ios_base::fmtflags f = os.flags();
		if (f & std::ios_base::oct) throw cybozu::Exception("fpT:operator<<:oct is not supported");
		const int base = (f & std::ios_base::hex) ? 16 : 10;
		const bool showBase = (f & std::ios_base::showbase) != 0;
		std::string str;
		self.getStr(str, base, showBase);
		return os << str;
	}
	friend inline std::istream& operator>>(std::istream& is, FpT& self)
	{
		const std::ios_base::fmtflags f = is.flags();
		if (f & std::ios_base::oct) throw cybozu::Exception("fpT:operator>>:oct is not supported");
		const int base = (f & std::ios_base::hex) ? 16 : 0;
		std::string str;
		is >> str;
		self.setStr(str, base);
		return is;
	}
	/*
		@note
		this compare functions is slow because of calling mul if useMont is true.
	*/
	static inline int compare(const FpT& x, const FpT& y)
	{
		fp::Block xb, yb;
		x.getBlock(xb);
		y.getBlock(yb);
		return fp::compareArray(xb.p, yb.p, op_.N);
	}
	bool isLess(const FpT& rhs) const
	{
		fp::Block xb, yb;
		getBlock(xb);
		rhs.getBlock(yb);
		return fp::isLessArray(xb.p, yb.p, op_.N);
	}
	bool operator<(const FpT& rhs) const { return isLess(rhs); }
	bool operator>=(const FpT& rhs) const { return !operator<(rhs); }
	bool operator>(const FpT& rhs) const { return rhs < *this; }
	bool operator<=(const FpT& rhs) const { return !operator>(rhs); }
	/*
		@note
		return unexpected order if useMont is set.
	*/
	static inline int compareRaw(const FpT& x, const FpT& y)
	{
		return fp::compareArray(x.v_, y.v_, op_.N);
	}
	bool isLessRaw(const FpT& rhs) const
	{
		return fp::isLessArray(v_, rhs.v_, op_.N);
	}
	/*
		wrapper function for generic p
		add(z, x, y)
		  case 1: op_.add(z.v_, x.v_, y.v_) written by Xbyak with fixed p
		  case 2: addW(z.v_, x.v_, y.v_)
		            op_.addP(z, x, y, p) written by GMP/LLVM with generic p
	*/
	static inline void addW(Unit *z, const Unit *x, const Unit *y)
	{
		op_.addP(z, x, y, op_.p);
	}
	static inline void subW(Unit *z, const Unit *x, const Unit *y)
	{
		op_.subP(z, x, y, op_.p);
	}
	static inline void mulW(Unit *z, const Unit *x, const Unit *y)
	{
		Unit xy[maxSize * 2];
		op_.mulPreP(xy, x, y);
		op_.modP(z, xy, op_.p);
	}
	static inline void sqrW(Unit *y, const Unit *x)
	{
		Unit xx[maxSize * 2];
		op_.sqrPreP(xx, x);
		op_.modP(y, xx, op_.p);
	}
	static inline void negW(Unit *y, const Unit *x)
	{
		op_.negP(y, x, op_.p);
	}
	// wrapper function for mcl_fp_mont by LLVM
	static inline void montW(Unit *z, const Unit *x, const Unit *y)
	{
		op_.mont(z, x, y, op_.p, op_.rp);
	}
	static inline void montSqrW(Unit *y, const Unit *x)
	{
		op_.mont(y, x, x, op_.p, op_.rp);
	}
	void normalize() {} // dummy method
};

template<class tag, size_t maxBitSize> fp::Op FpT<tag, maxBitSize>::op_;

} // mcl

namespace std { CYBOZU_NAMESPACE_TR1_BEGIN
template<class T> struct hash;

template<class tag, size_t maxBitSize>
struct hash<mcl::FpT<tag, maxBitSize> > : public std::unary_function<mcl::FpT<tag, maxBitSize>, size_t> {
	size_t operator()(const mcl::FpT<tag, maxBitSize>& x, uint64_t v = 0) const
	{
		return static_cast<size_t>(cybozu::hash64(x.getUnit(), x.getUnitSize(), v));
	}
};

CYBOZU_NAMESPACE_TR1_END } // std::tr1

#ifdef _WIN32
	#pragma warning(pop)
#endif
