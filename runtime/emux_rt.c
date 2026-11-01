/* Runtime numerico do Emux (so x86_64/win64).
 *
 * O compilador embute este fonte e compila-o com o gcc do utilizador no momento
 * da ligacao. Nao usa a libc: apenas operacoes inteiras e arrays na pilha.
 *
 * Formatos:
 *   - Floats fN: N bits (multiplo de 8), E bits de expoente, P = N - E bits de
 *     significando (bit implicito incluido), bias = 2^(E-1) - 1, sinal no bit
 *     N-1, mantissa nos bits 0..P-2. f16/f32/f64 coincidem com IEEE 754.
 *   - Ponto fixo qN.S: inteiro N bits em complemento para dois, little-endian,
 *     valor = cru / 2^S.
 *   - Todos os buffers sao little-endian, sem padding.
 *
 * Politica numerica dos floats:
 *   - arredondamento ao par (RNE);
 *   - FTZ: subnormais na entrada sao tratados como zero e resultados
 *     subnormais passam a zero com o sinal do resultado;
 *   - Inf e NaN seguem IEEE 754 (NaN de entrada propaga NaN canonico).
 *
 * ABI interna (chamadas do NASM gerado, ABI Microsoft x64):
 *   rcx = destino, rdx/r8/r9 = restantes argumentos (4 no maximo).
 *
 * Empacotamento do "spec" de um float: bits | (E << 20).
 */

#include <stddef.h>
#include <stdint.h>

#define RT_K 3u /* bits de guarda, arredondamento e sticky abaixo da mantissa */

#if defined(_WIN32) && defined(__x86_64__)
/* O gcc do MinGW chama ___chkstk_ms em frames grandes (incluindo arrays
 * variaveis). Como a ligacao usa -nostdlib, a rotina vem daqui. Sequencia
 * identica a da libgcc do mingw-w64. */
__asm__(
    ".text\n"
    ".globl ___chkstk_ms\n"
    "___chkstk_ms:\n"
    "    push %rcx\n"
    "    push %rax\n"
    "    cmp $0x1000, %rax\n"
    "    lea 0x18(%rsp), %rcx\n"
    "    jb 2f\n"
    "1:  sub $0x1000, %rcx\n"
    "    orq $0, (%rcx)\n"
    "    sub $0x1000, %rax\n"
    "    cmp $0x1000, %rax\n"
    "    ja 1b\n"
    "2:  sub %rax, %rcx\n"
    "    orq $0, (%rcx)\n"
    "    pop %rax\n"
    "    pop %rcx\n"
    "    ret\n"
);
#endif

/* O gcc pode transformar lacos em chamadas a estas rotinas. Ficam fracas para
 * que uma definicao do programa do utilizador prevaleça na ligacao. */
#if defined(__GNUC__)
#define RT_WEAK __attribute__((weak))
#else
#define RT_WEAK
#endif

RT_WEAK void* memcpy(void* dst, const void* src, size_t count)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return dst;
}

RT_WEAK void* memset(void* dst, int value, size_t count)
{
    unsigned char* d = (unsigned char*)dst;
    for (size_t i = 0; i < count; ++i) d[i] = (unsigned char)value;
    return dst;
}

RT_WEAK void* memmove(void* dst, const void* src, size_t count)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s)
    {
        for (size_t i = 0; i < count; ++i) d[i] = s[i];
    }
    else
    {
        for (size_t i = count; i-- > 0;) d[i] = s[i];
    }
    return dst;
}

RT_WEAK int memcmp(const void* a, const void* b, size_t count)
{
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (size_t i = 0; i < count; ++i)
    {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Utilidades de bytes (little-endian).                                       */
/* ------------------------------------------------------------------------- */

static void rt_zero(uint8_t* d, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) d[i] = 0;
}

static void rt_copy(uint8_t* d, const uint8_t* s, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) d[i] = s[i];
}

static int rt_is_zero(const uint8_t* a, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; ++i) acc |= a[i];
    return acc == 0;
}

static int rt_cmp(const uint8_t* a, const uint8_t* b, uint32_t n)
{
    for (uint32_t i = n; i-- > 0;)
    {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

static uint32_t rt_add(uint8_t* d, const uint8_t* a, const uint8_t* b, uint32_t n)
{
    uint32_t carry = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        const uint32_t sum = (uint32_t)a[i] + (uint32_t)b[i] + carry;
        d[i] = (uint8_t)sum;
        carry = sum >> 8;
    }
    return carry;
}

static void rt_sub(uint8_t* d, const uint8_t* a, const uint8_t* b, uint32_t n)
{
    int32_t borrow = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        const int32_t diff = (int32_t)a[i] - (int32_t)b[i] - borrow;
        d[i] = (uint8_t)(diff & 0xFF);
        borrow = diff < 0 ? 1 : 0;
    }
}

static void rt_neg(uint8_t* d, const uint8_t* a, uint32_t n)
{
    uint32_t carry = 1;
    for (uint32_t i = 0; i < n; ++i)
    {
        const uint32_t value = (uint32_t)(uint8_t)~a[i] + carry;
        d[i] = (uint8_t)value;
        carry = value >> 8;
    }
}

static int rt_is_negative(const uint8_t* a, uint32_t n)
{
    return n != 0 && (a[n - 1] & 0x80u) != 0;
}

static uint32_t rt_bit_get(const uint8_t* a, uint32_t index)
{
    return (uint32_t)(a[index >> 3] >> (index & 7)) & 1u;
}

static void rt_bit_set(uint8_t* a, uint32_t index, uint32_t value)
{
    const uint8_t mask = (uint8_t)(1u << (index & 7));
    if (value)
    {
        a[index >> 3] |= mask;
    }
    else
    {
        a[index >> 3] &= (uint8_t)~mask;
    }
}

static uint32_t rt_bit_len(const uint8_t* a, uint32_t n)
{
    for (uint32_t i = n; i-- > 0;)
    {
        if (a[i] != 0)
        {
            uint32_t bits = 0;
            uint8_t value = a[i];
            while (value)
            {
                value >>= 1;
                ++bits;
            }
            return i * 8 + bits;
        }
    }
    return 0;
}

/* d = s << count; d pode ser o mesmo buffer que s. */
static void rt_shl(uint8_t* d, const uint8_t* s, uint32_t n, uint32_t count)
{
    if ((uint64_t)count >= (uint64_t)n * 8)
    {
        rt_zero(d, n);
        return;
    }

    const uint32_t byte_shift = count >> 3;
    const uint32_t bit_shift = count & 7;

    for (uint32_t i = n; i-- > 0;)
    {
        uint32_t value = 0;
        const int32_t j = (int32_t)i - (int32_t)byte_shift;
        if (j >= 0)
        {
            value = (uint32_t)s[j] << bit_shift;
            if (bit_shift != 0 && j > 0)
            {
                value |= (uint32_t)s[j - 1] >> (8 - bit_shift);
            }
        }
        d[i] = (uint8_t)value;
    }
}

/* d = s >> count, com os bits descartados sinalizados no retorno. */
static uint32_t rt_shr_sticky(
    uint8_t* d,
    uint32_t dn,
    const uint8_t* s,
    uint32_t sn,
    uint64_t count
)
{
    const uint64_t total = (uint64_t)sn * 8;
    uint32_t sticky = 0;

    if (count >= total)
    {
        sticky = (uint32_t)!rt_is_zero(s, sn);
    }
    else
    {
        const uint32_t byte_shift = (uint32_t)(count >> 3);
        const uint32_t bit_shift = (uint32_t)(count & 7);

        for (uint32_t i = 0; i < byte_shift && i < sn; ++i)
        {
            sticky |= s[i];
        }

        if (bit_shift != 0 && byte_shift < sn)
        {
            sticky |= (uint32_t)s[byte_shift] & ((1u << bit_shift) - 1u);
        }
    }

    if (count >= total)
    {
        rt_zero(d, dn);
        return (uint32_t)(sticky != 0);
    }

    const uint32_t byte_shift = (uint32_t)(count >> 3);
    const uint32_t bit_shift = (uint32_t)(count & 7);

    for (uint32_t i = 0; i < dn; ++i)
    {
        const uint64_t j = (uint64_t)i + byte_shift;
        uint32_t value = j < sn ? (uint32_t)s[j] >> bit_shift : 0;
        if (bit_shift != 0 && j + 1 < sn)
        {
            value |= (uint32_t)s[j + 1] << (8 - bit_shift);
        }
        d[i] = (uint8_t)value;
    }

    return (uint32_t)(sticky != 0);
}

/* d = a * b em 2n bytes. */
static void rt_mul_full(uint8_t* d, const uint8_t* a, const uint8_t* b, uint32_t n)
{
    rt_zero(d, 2 * n);

    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t carry = 0;
        const uint32_t ai = a[i];

        for (uint32_t j = 0; j < n; ++j)
        {
            const uint32_t value = (uint32_t)d[i + j] + ai * (uint32_t)b[j] + carry;
            d[i + j] = (uint8_t)value;
            carry = value >> 8;
        }

        uint32_t k = i + n;
        while (carry != 0 && k < 2 * n)
        {
            const uint32_t value = (uint32_t)d[k] + carry;
            d[k] = (uint8_t)value;
            carry = value >> 8;
            ++k;
        }
    }
}

/* q = a / b e rem = a % b. rem precisa de bBytes + 1 bytes. */
static void rt_divmod(
    uint8_t* q,
    uint8_t* rem,
    const uint8_t* a,
    uint32_t aBits,
    const uint8_t* b,
    uint32_t bBytes
)
{
    const uint32_t rem_bytes = bBytes + 1;

    rt_zero(q, (aBits + 7u) / 8u);
    rt_zero(rem, rem_bytes);

    for (uint32_t i = aBits; i-- > 0;)
    {
        for (uint32_t k = rem_bytes; k-- > 1;)
        {
            rem[k] = (uint8_t)(((uint32_t)rem[k] << 1) | ((uint32_t)rem[k - 1] >> 7));
        }
        rem[0] = (uint8_t)(rem[0] << 1);
        rem[0] |= (uint8_t)rt_bit_get(a, i);

        const int ge = rem[bBytes] != 0 || rt_cmp(rem, b, bBytes) >= 0;

        if (ge)
        {
            int32_t borrow = 0;
            for (uint32_t k = 0; k < rem_bytes; ++k)
            {
                const int32_t bk = k < bBytes ? (int32_t)b[k] : 0;
                const int32_t diff = (int32_t)rem[k] - bk - borrow;
                rem[k] = (uint8_t)(diff & 0xFF);
                borrow = diff < 0 ? 1 : 0;
            }
            rt_bit_set(q, i, 1);
        }
    }
}

/* Acrescenta 1 na posicao `index` de x (com carry ate `bytes`). */
static void rt_add_at(uint8_t* x, uint32_t bytes, uint32_t index)
{
    uint32_t byte_index = index >> 3;
    uint32_t add = 1u << (index & 7);

    while (byte_index < bytes)
    {
        const uint32_t value = (uint32_t)x[byte_index] + add;
        x[byte_index] = (uint8_t)value;
        if (value <= 0xFFu)
        {
            return;
        }
        add = 1;
        ++byte_index;
    }
}

/* ------------------------------------------------------------------------- */
/* Floats.                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct
{
    uint32_t bits;
    uint32_t exponent_bits;
    uint32_t precision; /* P, incluindo o bit implicito */
    uint32_t bytes;
    uint32_t bias;
    uint64_t exponent_max;
} rt_fmt;

enum
{
    RT_FINITE = 0,
    RT_ZERO = 1,
    RT_INF = 2,
    RT_NAN = 3
};

static rt_fmt rt_make_fmt(uint64_t spec)
{
    rt_fmt fmt;
    fmt.bits = (uint32_t)(spec & 0xFFFFFu);
    fmt.exponent_bits = (uint32_t)((spec >> 20) & 0x3Fu);
    fmt.precision = fmt.bits - fmt.exponent_bits;
    fmt.bytes = fmt.bits / 8;
    fmt.bias = (uint32_t)(((uint64_t)1 << (fmt.exponent_bits - 1)) - 1);
    fmt.exponent_max = ((uint64_t)1 << fmt.exponent_bits) - 1;
    return fmt;
}

static int rt_f_decode(
    const uint8_t* a,
    const rt_fmt* fmt,
    uint32_t* sign,
    int64_t* exponent,
    uint8_t* significand,
    uint32_t significand_bytes
)
{
    *sign = rt_bit_get(a, fmt->bits - 1);

    uint64_t field = 0;
    for (uint32_t i = 0; i < fmt->exponent_bits; ++i)
    {
        field |= (uint64_t)rt_bit_get(a, fmt->precision - 1 + i) << i;
    }

    rt_zero(significand, significand_bytes);
    for (uint32_t i = 0; i + 1 < fmt->precision; ++i)
    {
        rt_bit_set(significand, i, rt_bit_get(a, i));
    }

    if (field == fmt->exponent_max)
    {
        uint32_t mantissa = 0;
        for (uint32_t i = 0; i + 1 < fmt->precision; ++i)
        {
            mantissa |= rt_bit_get(a, i);
        }
        return mantissa != 0 ? RT_NAN : RT_INF;
    }

    if (field == 0)
    {
        /* FTZ: subnormal vira zero. */
        return RT_ZERO;
    }

    rt_bit_set(significand, fmt->precision - 1, 1);
    *exponent = (int64_t)field - (int64_t)fmt->bias;
    return RT_FINITE;
}

static void rt_f_write_zero(uint8_t* dst, const rt_fmt* fmt, uint32_t sign)
{
    rt_zero(dst, fmt->bytes);
    rt_bit_set(dst, fmt->bits - 1, sign);
}

static void rt_f_write_inf(uint8_t* dst, const rt_fmt* fmt, uint32_t sign)
{
    rt_zero(dst, fmt->bytes);
    rt_bit_set(dst, fmt->bits - 1, sign);
    for (uint32_t i = 0; i < fmt->exponent_bits; ++i)
    {
        rt_bit_set(dst, fmt->precision - 1 + i, 1);
    }
}

static void rt_f_write_nan(uint8_t* dst, const rt_fmt* fmt)
{
    rt_zero(dst, fmt->bytes);
    for (uint32_t i = 0; i < fmt->exponent_bits; ++i)
    {
        rt_bit_set(dst, fmt->precision - 1 + i, 1);
    }
    rt_bit_set(dst, fmt->precision - 2, 1); /* bit mais alto da mantissa */
}

/* Normaliza x (com o bit implicito em P+K-1), arredonda (RNE), aplica FTZ e
 * escreve o resultado no formato fmt. `base` e o expoente do valor 1.f.
 * `sticky` marca bits positivos perdidos abaixo de x; `tail` (0, +1, -1)
 * marca um residuo infinitesimal cujo sinal nao cabe em `sticky` — a
 * subtracao de um subtraendo truncado deixa o valor exato abaixo de x. */
static void rt_f_finalize(
    uint8_t* dst,
    const rt_fmt* fmt,
    uint8_t* x,
    uint32_t width,
    int64_t base,
    uint32_t sticky,
    int tail,
    uint32_t sign,
    uint8_t* scratch
)
{
    const uint32_t target = fmt->precision + RT_K;
    uint32_t length = rt_bit_len(x, width);

    if (length == 0)
    {
        rt_f_write_zero(dst, fmt, sign);
        return;
    }

    if (length > target)
    {
        const uint32_t shift = length - target;
        sticky |= rt_shr_sticky(scratch, width, x, width, shift);
        rt_copy(x, scratch, width);
        base += (int64_t)shift;
        length = target;
    }
    else if (length < target)
    {
        const uint32_t shift = target - length;
        rt_shl(scratch, x, width, shift);
        rt_copy(x, scratch, width);
        base -= (int64_t)shift;
    }

    if (base + (int64_t)fmt->bias >= (int64_t)fmt->exponent_max)
    {
        rt_f_write_inf(dst, fmt, sign);
        return;
    }

    if (base + (int64_t)fmt->bias <= 0)
    {
        rt_f_write_zero(dst, fmt, sign);
        return;
    }

    const uint32_t guard = rt_bit_get(x, RT_K - 1);
    const uint32_t round = rt_bit_get(x, RT_K - 2);
    const uint32_t low = rt_bit_get(x, RT_K);
    const uint32_t lost = sticky | rt_bit_get(x, 0);

    /* Em empate exato (fracao == 1/2) o sinal do residuo decide; sem
     * residuo vale o par (RNE). Nos outros casos ele nao muda o resultado. */
    const int roundUp = guard &&
        ((!round && !lost) ? (tail > 0 || (tail == 0 && low)) : 1);

    if (roundUp)
    {
        rt_add_at(x, width, RT_K);

        if (rt_bit_get(x, fmt->precision + RT_K))
        {
            rt_shr_sticky(scratch, width, x, width, 1);
            rt_copy(x, scratch, width);
            base += 1;

            if (base + (int64_t)fmt->bias >= (int64_t)fmt->exponent_max)
            {
                rt_f_write_inf(dst, fmt, sign);
                return;
            }
        }
    }

    const uint64_t field = (uint64_t)(base + (int64_t)fmt->bias);

    rt_zero(dst, fmt->bytes);
    rt_bit_set(dst, fmt->bits - 1, sign);
    for (uint32_t i = 0; i < fmt->exponent_bits; ++i)
    {
        rt_bit_set(dst, fmt->precision - 1 + i, (uint32_t)((field >> i) & 1u));
    }
    for (uint32_t i = 0; i + 1 < fmt->precision; ++i)
    {
        rt_bit_set(dst, i, rt_bit_get(x, RT_K + i));
    }
}

static void rt_f_addsub(
    uint8_t* dst,
    const uint8_t* a,
    const uint8_t* b,
    uint64_t spec,
    int subtract
)
{
    const rt_fmt fmt = rt_make_fmt(spec);
    const uint32_t width = (fmt.precision + RT_K + 1 + 7u) / 8u;

    uint8_t sig_a[width];
    uint8_t sig_b[width];
    uint8_t op_a[width];
    uint8_t op_b[width];
    uint8_t x[width];
    uint8_t scratch[width];

    uint32_t sign_a = 0;
    uint32_t sign_b = 0;
    int64_t exp_a = 0;
    int64_t exp_b = 0;

    const int class_a = rt_f_decode(a, &fmt, &sign_a, &exp_a, sig_a, width);
    int class_b = rt_f_decode(b, &fmt, &sign_b, &exp_b, sig_b, width);

    if (class_a == RT_NAN || class_b == RT_NAN)
    {
        rt_f_write_nan(dst, &fmt);
        return;
    }

    const uint32_t sign_b_effective = sign_b ^ (uint32_t)(subtract != 0);

    if (class_a == RT_INF || class_b == RT_INF)
    {
        if (class_a == RT_INF && class_b == RT_INF && sign_a != sign_b_effective)
        {
            rt_f_write_nan(dst, &fmt);
            return;
        }
        rt_f_write_inf(dst, &fmt, class_a == RT_INF ? sign_a : sign_b_effective);
        return;
    }

    if (class_a == RT_ZERO && class_b == RT_ZERO)
    {
        rt_f_write_zero(dst, &fmt, sign_a & sign_b_effective);
        return;
    }

    if (class_a == RT_ZERO)
    {
        rt_copy(dst, b, fmt.bytes);
        if (sign_b_effective != sign_b)
        {
            dst[fmt.bytes - 1] ^= 0x80u;
        }
        return;
    }

    if (class_b == RT_ZERO)
    {
        rt_copy(dst, a, fmt.bytes);
        return;
    }

    /* Alinha os dois significandos no maior expoente, com K bits extra. */
    const int64_t base = exp_a > exp_b ? exp_a : exp_b;
    uint32_t sticky = 0;

    rt_shl(op_a, sig_a, width, RT_K);
    rt_shl(op_b, sig_b, width, RT_K);

    const uint64_t align_a = (uint64_t)(base - exp_a);
    const uint64_t align_b = (uint64_t)(base - exp_b);

    if (align_a != 0)
    {
        sticky |= rt_shr_sticky(op_a, width, op_a, width, align_a);
    }

    if (align_b != 0)
    {
        sticky |= rt_shr_sticky(op_b, width, op_b, width, align_b);
    }

    if (sign_a == sign_b_effective)
    {
        rt_add(x, op_a, op_b, width);
        rt_f_finalize(dst, &fmt, x, width, base, sticky, 0, sign_a, scratch);
        return;
    }

    /* Subtracao: o subtraendo (sempre o de menor expoente) foi truncado para
     * baixo, logo o valor exato fica abaixo da diferenca calculada. */
    const int tail = sticky != 0 ? -1 : 0;
    const int order = rt_cmp(op_a, op_b, width);

    if (order == 0)
    {
        rt_f_write_zero(dst, &fmt, 0);
    }
    else if (order > 0)
    {
        rt_sub(x, op_a, op_b, width);
        rt_f_finalize(dst, &fmt, x, width, base, 0, tail, sign_a, scratch);
    }
    else
    {
        rt_sub(x, op_b, op_a, width);
        rt_f_finalize(dst, &fmt, x, width, base, 0, tail, sign_b_effective, scratch);
    }
}

static void rt_f_mul(uint8_t* dst, const uint8_t* a, const uint8_t* b, uint64_t spec)
{
    const rt_fmt fmt = rt_make_fmt(spec);
    const uint32_t width = (fmt.precision + RT_K + 1 + 7u) / 8u;

    uint8_t sig_a[width];
    uint8_t sig_b[width];
    uint8_t product[2 * width];
    uint8_t x[width];
    uint8_t scratch[width];

    uint32_t sign_a = 0;
    uint32_t sign_b = 0;
    int64_t exp_a = 0;
    int64_t exp_b = 0;

    const int class_a = rt_f_decode(a, &fmt, &sign_a, &exp_a, sig_a, width);
    const int class_b = rt_f_decode(b, &fmt, &sign_b, &exp_b, sig_b, width);
    const uint32_t sign = sign_a ^ sign_b;

    if (class_a == RT_NAN || class_b == RT_NAN)
    {
        rt_f_write_nan(dst, &fmt);
        return;
    }

    if (class_a == RT_INF || class_b == RT_INF)
    {
        if (class_a == RT_ZERO || class_b == RT_ZERO)
        {
            rt_f_write_nan(dst, &fmt);
            return;
        }
        rt_f_write_inf(dst, &fmt, sign);
        return;
    }

    if (class_a == RT_ZERO || class_b == RT_ZERO)
    {
        rt_f_write_zero(dst, &fmt, sign);
        return;
    }

    rt_mul_full(product, sig_a, sig_b, width);

    const uint32_t top = rt_bit_len(product, 2 * width);
    const uint32_t target = fmt.precision + RT_K;
    const int64_t base =
        exp_a + exp_b - 2 * (int64_t)fmt.precision + 2 + (int64_t)(top - 1);

    uint32_t sticky = 0;
    rt_zero(x, width);

    if (top > target)
    {
        sticky = rt_shr_sticky(x, width, product, 2 * width, top - target);
    }
    else if (top < target)
    {
        rt_shl(x, product, width, target - top);
    }

    rt_f_finalize(dst, &fmt, x, width, base, sticky, 0, sign, scratch);
}

static void rt_f_div(uint8_t* dst, const uint8_t* a, const uint8_t* b, uint64_t spec)
{
    const rt_fmt fmt = rt_make_fmt(spec);
    const uint32_t width = (fmt.precision + RT_K + 1 + 7u) / 8u;

    uint8_t sig_a[width];
    uint8_t sig_b[width];
    uint8_t x[width];
    uint8_t scratch[width];

    uint32_t sign_a = 0;
    uint32_t sign_b = 0;
    int64_t exp_a = 0;
    int64_t exp_b = 0;

    const int class_a = rt_f_decode(a, &fmt, &sign_a, &exp_a, sig_a, width);
    const int class_b = rt_f_decode(b, &fmt, &sign_b, &exp_b, sig_b, width);
    const uint32_t sign = sign_a ^ sign_b;

    if (class_a == RT_NAN || class_b == RT_NAN)
    {
        rt_f_write_nan(dst, &fmt);
        return;
    }

    if (class_a == RT_INF && class_b == RT_INF)
    {
        rt_f_write_nan(dst, &fmt);
        return;
    }

    if (class_a == RT_INF)
    {
        rt_f_write_inf(dst, &fmt, sign);
        return;
    }

    if (class_b == RT_INF)
    {
        rt_f_write_zero(dst, &fmt, sign);
        return;
    }

    if (class_b == RT_ZERO)
    {
        if (class_a == RT_ZERO)
        {
            rt_f_write_nan(dst, &fmt);
        }
        else
        {
            rt_f_write_inf(dst, &fmt, sign);
        }
        return;
    }

    if (class_a == RT_ZERO)
    {
        rt_f_write_zero(dst, &fmt, sign);
        return;
    }

    const uint32_t target = fmt.precision + RT_K;
    const uint32_t dividend_bits = fmt.precision + target;
    const uint32_t dividend_bytes = (dividend_bits + 7u) / 8u;

    uint8_t dividend[dividend_bytes];
    uint8_t quotient[dividend_bytes];
    uint8_t remainder[width + 1];

    rt_zero(dividend, dividend_bytes);
    rt_copy(dividend, sig_a, width);
    rt_shl(dividend, dividend, dividend_bytes, target);

    rt_divmod(quotient, remainder, dividend, dividend_bits, sig_b, width);

    const uint32_t top = rt_bit_len(quotient, dividend_bytes);
    /* O resto e positivo: a razao exata fica acima do quociente truncado. */
    uint32_t sticky = !rt_is_zero(remainder, width + 1);

    rt_zero(x, width);

    if (top > target)
    {
        sticky |= rt_shr_sticky(x, width, quotient, dividend_bytes, top - target);
    }
    else
    {
        rt_copy(x, quotient, width);
    }

    int64_t base = exp_a - exp_b + (int64_t)top
        - (int64_t)fmt.precision - (int64_t)RT_K - 1;

    if (top < target)
    {
        rt_shl(scratch, x, width, target - top);
        rt_copy(x, scratch, width);
        base -= (int64_t)(target - top);
    }

    rt_f_finalize(dst, &fmt, x, width, base, sticky, 0, sign, scratch);
}

static void rt_f_convert(
    uint8_t* dst,
    const uint8_t* src,
    uint64_t dst_spec,
    uint64_t src_spec
)
{
    const rt_fmt dst_fmt = rt_make_fmt(dst_spec);
    const rt_fmt src_fmt = rt_make_fmt(src_spec);

    const uint32_t src_width = (src_fmt.precision + RT_K + 1 + 7u) / 8u;
    const uint32_t dst_width = (dst_fmt.precision + RT_K + 1 + 7u) / 8u;
    const uint32_t width = src_width > dst_width ? src_width : dst_width;

    uint8_t significand[src_width];
    uint8_t x[width];
    uint8_t scratch[width];

    uint32_t sign = 0;
    int64_t exponent = 0;

    const int class_src =
        rt_f_decode(src, &src_fmt, &sign, &exponent, significand, src_width);

    if (class_src == RT_NAN)
    {
        rt_f_write_nan(dst, &dst_fmt);
        return;
    }

    if (class_src == RT_INF)
    {
        rt_f_write_inf(dst, &dst_fmt, sign);
        return;
    }

    if (class_src == RT_ZERO)
    {
        rt_f_write_zero(dst, &dst_fmt, sign);
        return;
    }

    rt_zero(x, width);
    rt_copy(x, significand, src_width);
    rt_shl(x, x, width, RT_K);

    int64_t base = exponent;
    uint32_t sticky = 0;

    const uint32_t source_top = src_fmt.precision + RT_K;
    const uint32_t target = dst_fmt.precision + RT_K;

    /* O offset implicito muda junto com P, por isso o expoente nao se altera:
     * x cresce/diminui 2^shift e 2^-(P+K-1) compensa exatamente. */
    if (source_top > target)
    {
        const uint32_t shift = source_top - target;
        sticky = rt_shr_sticky(scratch, width, x, width, shift);
        rt_copy(x, scratch, width);
    }
    else if (source_top < target)
    {
        const uint32_t shift = target - source_top;
        rt_shl(scratch, x, width, shift);
        rt_copy(x, scratch, width);
    }

    rt_f_finalize(dst, &dst_fmt, x, width, base, sticky, 0, sign, scratch);
}

static void rt_f_from_int(
    uint8_t* dst,
    const uint8_t* src,
    uint32_t src_bytes,
    uint32_t src_signed,
    uint64_t spec
)
{
    const rt_fmt fmt = rt_make_fmt(spec);
    const uint32_t width = (fmt.precision + RT_K + 1 + 7u) / 8u;

    uint8_t magnitude[src_bytes];
    uint8_t x[width];
    uint8_t scratch[width];

    uint32_t sign = 0;

    if (src_signed && rt_is_negative(src, src_bytes))
    {
        rt_neg(magnitude, src, src_bytes);
        sign = 1;
    }
    else
    {
        rt_copy(magnitude, src, src_bytes);
    }

    if (rt_is_zero(magnitude, src_bytes))
    {
        rt_f_write_zero(dst, &fmt, sign);
        return;
    }

    const uint32_t length = rt_bit_len(magnitude, src_bytes);
    const uint32_t target = fmt.precision + RT_K;
    uint32_t sticky = 0;

    rt_zero(x, width);

    if (length <= target)
    {
        const uint32_t copy_bytes = src_bytes < width ? src_bytes : width;
        rt_copy(x, magnitude, copy_bytes);
        rt_shl(x, x, width, target - length);
    }
    else
    {
        sticky = rt_shr_sticky(x, width, magnitude, src_bytes, length - target);
    }

    rt_f_finalize(dst, &fmt, x, width, (int64_t)length - 1, sticky, 0, sign, scratch);
}

/* Magnitude de um valor escalado (sig * 2^shift) truncada em dst_bytes. */
static void rt_scaled_magnitude(
    uint8_t* dst,
    uint32_t dst_bytes,
    const uint8_t* sig,
    uint32_t sig_bytes,
    int64_t shift
)
{
    rt_zero(dst, dst_bytes);

    if (shift >= 0)
    {
        if ((uint64_t)shift >= (uint64_t)dst_bytes * 8)
        {
            return;
        }

        const uint32_t copy_bytes = sig_bytes < dst_bytes ? sig_bytes : dst_bytes;
        rt_copy(dst, sig, copy_bytes);
        rt_shl(dst, dst, dst_bytes, (uint32_t)shift);
        return;
    }

    const uint64_t amount = (uint64_t)(-shift);
    if (amount >= (uint64_t)sig_bytes * 8)
    {
        return;
    }

    (void)rt_shr_sticky(dst, dst_bytes, sig, sig_bytes, amount);
}

static void rt_f_to_int(
    uint8_t* dst,
    uint32_t dst_bytes,
    const uint8_t* src,
    uint64_t spec
)
{
    const rt_fmt fmt = rt_make_fmt(spec);
    const uint32_t width = (fmt.precision + RT_K + 1 + 7u) / 8u;

    uint8_t significand[width];

    uint32_t sign = 0;
    int64_t exponent = 0;

    const int class_src =
        rt_f_decode(src, &fmt, &sign, &exponent, significand, width);

    rt_zero(dst, dst_bytes);

    /* NaN/Inf nao tem representacao inteira: resultado definido como 0. */
    if (class_src != RT_FINITE)
    {
        return;
    }

    const int64_t shift = exponent - (int64_t)(fmt.precision - 1);
    rt_scaled_magnitude(dst, dst_bytes, significand, width, shift);

    if (sign)
    {
        rt_neg(dst, dst, dst_bytes);
    }
}

static void rt_f_from_fixed(
    uint8_t* dst,
    const uint8_t* src,
    uint32_t src_bytes,
    uint32_t scale,
    uint64_t spec
)
{
    const rt_fmt fmt = rt_make_fmt(spec);
    const uint32_t width = (fmt.precision + RT_K + 1 + 7u) / 8u;

    uint8_t magnitude[src_bytes];
    uint8_t x[width];
    uint8_t scratch[width];

    uint32_t sign = 0;

    if (rt_is_negative(src, src_bytes))
    {
        rt_neg(magnitude, src, src_bytes);
        sign = 1;
    }
    else
    {
        rt_copy(magnitude, src, src_bytes);
    }

    if (rt_is_zero(magnitude, src_bytes))
    {
        rt_f_write_zero(dst, &fmt, sign);
        return;
    }

    const uint32_t length = rt_bit_len(magnitude, src_bytes);
    const uint32_t target = fmt.precision + RT_K;
    uint32_t sticky = 0;

    rt_zero(x, width);

    if (length <= target)
    {
        const uint32_t copy_bytes = src_bytes < width ? src_bytes : width;
        rt_copy(x, magnitude, copy_bytes);
        rt_shl(x, x, width, target - length);
    }
    else
    {
        sticky = rt_shr_sticky(x, width, magnitude, src_bytes, length - target);
    }

    const int64_t base = (int64_t)length - 1 - (int64_t)scale;
    rt_f_finalize(dst, &fmt, x, width, base, sticky, 0, sign, scratch);
}

static void rt_f_to_fixed(
    uint8_t* dst,
    uint32_t dst_bytes,
    uint32_t scale,
    const uint8_t* src,
    uint64_t spec
)
{
    const rt_fmt fmt = rt_make_fmt(spec);
    const uint32_t width = (fmt.precision + RT_K + 1 + 7u) / 8u;

    uint8_t significand[width];

    uint32_t sign = 0;
    int64_t exponent = 0;

    const int class_src =
        rt_f_decode(src, &fmt, &sign, &exponent, significand, width);

    rt_zero(dst, dst_bytes);

    if (class_src != RT_FINITE)
    {
        return;
    }

    const int64_t shift =
        exponent - (int64_t)(fmt.precision - 1) + (int64_t)scale;
    rt_scaled_magnitude(dst, dst_bytes, significand, width, shift);

    if (sign)
    {
        rt_neg(dst, dst, dst_bytes);
    }
}

/* ------------------------------------------------------------------------- */
/* Ponto fixo.                                                                */
/* ------------------------------------------------------------------------- */

void __emux_q_rescale(void* dst_ptr, const void* src_ptr, uint64_t packed, int32_t shift)
{
    uint8_t* dst = (uint8_t*)dst_ptr;
    const uint8_t* src = (const uint8_t*)src_ptr;
    const uint32_t dst_bytes = (uint32_t)(packed & 0xFFFFu);
    const uint32_t src_bytes = (uint32_t)((packed >> 16) & 0xFFFFu);
    const uint32_t src_signed = (uint32_t)((packed >> 32) & 1u);

    /* Fontes sem sinal (u64 -> qN.K) nunca sao negativas, mesmo com o bit
     * alto ligado. */
    const int negative = src_signed && rt_is_negative(src, src_bytes);
    const uint32_t extra = shift > 0
        ? (uint32_t)(((uint32_t)shift + 7u) / 8u + 1u)
        : 0u;
    const uint32_t work_bytes = src_bytes + extra;

    uint8_t work[work_bytes];
    rt_zero(work, work_bytes);

    if (negative)
    {
        rt_neg(work, src, src_bytes);
    }
    else
    {
        rt_copy(work, src, src_bytes);
    }

    if (shift > 0)
    {
        rt_shl(work, work, work_bytes, (uint32_t)shift);
    }
    else if (shift < 0)
    {
        const uint64_t amount = (uint64_t)(-(int64_t)shift);
        if (amount >= (uint64_t)work_bytes * 8)
        {
            rt_zero(work, work_bytes);
        }
        else
        {
            (void)rt_shr_sticky(work, work_bytes, work, work_bytes, amount);
        }
    }

    rt_zero(dst, dst_bytes);
    const uint32_t copy_bytes = work_bytes < dst_bytes ? work_bytes : dst_bytes;
    rt_copy(dst, work, copy_bytes);

    if (negative && !rt_is_zero(dst, dst_bytes))
    {
        rt_neg(dst, dst, dst_bytes);
    }
}

void __emux_q_mul(void* dst_ptr, const void* a_ptr, const void* b_ptr, uint64_t packed)
{
    uint8_t* dst = (uint8_t*)dst_ptr;
    const uint8_t* a = (const uint8_t*)a_ptr;
    const uint8_t* b = (const uint8_t*)b_ptr;
    const uint32_t bytes = (uint32_t)(packed & 0xFFFFu);
    const uint32_t scale = (uint32_t)((packed >> 32) & 0xFFFFu);

    const int sign_a = rt_is_negative(a, bytes);
    const int sign_b = rt_is_negative(b, bytes);

    uint8_t mag_a[bytes];
    uint8_t mag_b[bytes];

    if (sign_a) rt_neg(mag_a, a, bytes); else rt_copy(mag_a, a, bytes);
    if (sign_b) rt_neg(mag_b, b, bytes); else rt_copy(mag_b, b, bytes);

    uint8_t product[2 * bytes];
    rt_mul_full(product, mag_a, mag_b, bytes);

    uint8_t result[bytes];
    rt_zero(result, bytes);
    (void)rt_shr_sticky(result, bytes, product, 2 * bytes, scale);

    if ((sign_a ^ sign_b) && !rt_is_zero(result, bytes))
    {
        rt_neg(result, result, bytes);
    }

    rt_copy(dst, result, bytes);
}

void __emux_q_div(void* dst_ptr, const void* a_ptr, const void* b_ptr, uint64_t packed)
{
    uint8_t* dst = (uint8_t*)dst_ptr;
    const uint8_t* a = (const uint8_t*)a_ptr;
    const uint8_t* b = (const uint8_t*)b_ptr;
    const uint32_t bytes = (uint32_t)(packed & 0xFFFFu);
    const uint32_t scale = (uint32_t)((packed >> 32) & 0xFFFFu);

    rt_zero(dst, bytes);

    if (rt_is_zero(b, bytes))
    {
        return;
    }

    const int sign_a = rt_is_negative(a, bytes);
    const int sign_b = rt_is_negative(b, bytes);

    uint8_t mag_a[bytes];
    uint8_t mag_b[bytes];

    if (sign_a) rt_neg(mag_a, a, bytes); else rt_copy(mag_a, a, bytes);
    if (sign_b) rt_neg(mag_b, b, bytes); else rt_copy(mag_b, b, bytes);

    const uint32_t numerator_bytes = bytes + (scale + 7u) / 8u + 1u;

    uint8_t numerator[numerator_bytes];
    rt_zero(numerator, numerator_bytes);
    rt_copy(numerator, mag_a, bytes);
    rt_shl(numerator, numerator, numerator_bytes, scale);

    uint8_t quotient[numerator_bytes];
    uint8_t remainder[bytes + 1];

    /* rt_divmod so zera ceil(bits/8) bytes de q; o buffer e maior, logo o
     * resto precisa ser zerado aqui para nao vazar lixo na copia final. */
    rt_zero(quotient, numerator_bytes);

    const uint32_t bits = rt_bit_len(numerator, numerator_bytes);
    if (bits == 0)
    {
        return;
    }

    rt_divmod(quotient, remainder, numerator, bits, mag_b, bytes);

    uint8_t result[bytes];
    rt_zero(result, bytes);
    rt_copy(result, quotient, bytes < numerator_bytes ? bytes : numerator_bytes);

    if ((sign_a ^ sign_b) && !rt_is_zero(result, bytes))
    {
        rt_neg(result, result, bytes);
    }

    rt_copy(dst, result, bytes);
}

/* ------------------------------------------------------------------------- */
/* Entradas chamadas pelo NASM gerado.                                       */
/* ------------------------------------------------------------------------- */

void __emux_f_add(void* dst, const void* a, const void* b, uint64_t spec)
{
    rt_f_addsub((uint8_t*)dst, (const uint8_t*)a, (const uint8_t*)b, spec, 0);
}

void __emux_f_sub(void* dst, const void* a, const void* b, uint64_t spec)
{
    rt_f_addsub((uint8_t*)dst, (const uint8_t*)a, (const uint8_t*)b, spec, 1);
}

void __emux_f_mul(void* dst, const void* a, const void* b, uint64_t spec)
{
    rt_f_mul((uint8_t*)dst, (const uint8_t*)a, (const uint8_t*)b, spec);
}

void __emux_f_div(void* dst, const void* a, const void* b, uint64_t spec)
{
    rt_f_div((uint8_t*)dst, (const uint8_t*)a, (const uint8_t*)b, spec);
}

void __emux_f_from_int(
    void* dst,
    const void* src,
    uint64_t packed,
    uint64_t spec
)
{
    rt_f_from_int(
        (uint8_t*)dst,
        (const uint8_t*)src,
        (uint32_t)(packed & 0xFFFFu),
        (uint32_t)((packed >> 16) & 1u),
        spec
    );
}

void __emux_f_to_int(void* dst, uint32_t dst_bytes, const void* src, uint64_t spec)
{
    rt_f_to_int((uint8_t*)dst, dst_bytes, (const uint8_t*)src, spec);
}

void __emux_f_convert(void* dst, const void* src, uint64_t dst_spec, uint64_t src_spec)
{
    rt_f_convert((uint8_t*)dst, (const uint8_t*)src, dst_spec, src_spec);
}

void __emux_f_from_fixed(
    void* dst,
    const void* src,
    uint64_t packed,
    uint64_t spec
)
{
    rt_f_from_fixed(
        (uint8_t*)dst,
        (const uint8_t*)src,
        (uint32_t)(packed & 0xFFFFu),
        (uint32_t)((packed >> 16) & 0xFFFFu),
        spec
    );
}

void __emux_f_to_fixed(
    void* dst,
    const void* src,
    uint64_t packed,
    uint64_t spec
)
{
    rt_f_to_fixed(
        (uint8_t*)dst,
        (uint32_t)(packed & 0xFFFFu),
        (uint32_t)((packed >> 16) & 0xFFFFu),
        (const uint8_t*)src,
        spec
    );
}
