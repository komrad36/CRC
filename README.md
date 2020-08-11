Computing is just the process of transforming and moving data. When data is moved through a channel or stored in a medium, there is some possibility of corruption: the chance that when the data is read back out on the receiving end, it's wrong - different than what was sent/stored. If we care enough about the data, we may want to perform some kind of verification to at least detect, if not correct, errors.

We often do care about our data's correctness, so the problem of verifying integrity proves quite ubiquitous. This very text likely made its way through MANY such verifications on its way to you - the data transfer mechanisms of Wi-Fi, Ethernet, and HDMI all verify every bit of data sent through them, for example.

Modern game engines and development tools are so complex that it should be no surprise that the problem is applicable to them in multiple ways.

Saved games are one such example. When we write out your progress to a disk drive, we might want to verify that it was written successfully. And when we load it, we might want to verify that it's still intact, lest we load bad data and cause crashes or bugs.

There are many more examples. A common class of bug is a "memory stomp", where we suffer a crash or bug because some data in memory got accidentally overwritten by some other code that wrote into memory it shouldn't have. These can be non-obvious, and it can aid debugging to verify a block of memory frequently, checking if it has changed, to catch any stomps into it right away.

Multiplayer profile data. Game assets conditioned and prepared for use by the game engine. Complete game images for digital download. Patches. The list goes on.

One thing at least some of these applications have in common is that they have to be FAST. Saved game data can be quite large, for example, and game consoles are not very powerful (and the CPU is already busy running the game!), so it also becomes imperative to do this verification efficiently. Similarly, the more efficient the verification procedure, the more frequently we can verify blocks of suspect memory without taking up so much CPU time that we slow down the engine or affect internal timings so much that a timing-related bug becomes harder to reproduce.

So we have some data we want to store or transmit reliably. Let's call it the "message", or M for short. We want to store a small (fixed-size) amount of additional information, or metadata, alongside the message that will help the receiver verify that the data is likely still correct. What do we store?

Before we begin, please note that code implementations, connected to a test framework to measure correctness and performance, are provided for ALL of the different options we will explore, whether in assembly or in C++. They accompany this document.

Let's think about it from first principles. M will be represented as a binary stream, so no matter what kind of data it is or how long it is, we can treat it as just a big serial blob of bits (0s and 1s), like this, for example:

```
    M = 0000001101001001
```

Let's say that no matter how large M is, we take the first 32 bits, and store that as our metadata. At the receiver, we check whether it matches the actual first 32 bits.

This would catch any error in the first 32 bits of the message, unless the exact same corruption occurred to the first 32 bits of the message AND the metadata.

Because as the size of M grows and the metadata remains fixed size, the metadata represents a smaller and smaller proportion of the total transmission (and therefore the surface area for corruption), we simplify the analysis by assuming the metadata itself will never get corrupted.

So in the case where we use the first 32 bits of the message as our metadata, we will catch 100% of instances of corruption of the first 32 bits of the message. Unfortunately, we will catch 0% of instances of corruption of the rest of the message, however large it is. No good.

We will need to be okay with a less than 100% catch rate in order to be able to reliably detect errors across the whole message, since we can only store a limited amount of metadata (in this case 32 bits).

Additional metadata stored alongside a message is sometimes called a checksum. As the name suggests, another approach that might work better is to just add up the whole message, 32 bits at a time, keeping just the low 32 bits of the sum, and use THAT as our metadata.

Let's use an example in decimal for simplicity. Instead of a 32 bit checksum, let's use a 2 digit checksum in base 10 (decimal). Say we had the message:

```
    M = 20417792
```

We break the message into 2 digit chunks, since that's the size of our checksum:

```
    M = 20 41 77 92
```

We add the chunks together:

```
    sum(M) = 230
```

We take just the low 2 digits since that's all we can fit in our checksum, which we will call C:

```
    C = 30
```

We would therefore transmit M and C together instead of just M:

```
    transmission = 20 41 77 92 30
```

The receiver would perform the same operation on the message part and confirm that it equaled C. If it did not, the receiver would know the message had been corrupted, and could discard it, request a retransmission, etc.

For example, if the second pair of digits had been misdelivered as 42 instead of 41, the receiver would see:

```
    received = 20 42 77 92 30
```

It would sum up the message and get:

```
    C' = 31
```

And it would know the message had been corrupted.

But what if the corruption manifested as one pair of digits increasing by 1, but another pair decreasing by 1? That would go undetected. Or what if two pairs of digits just got switched around in place? Or what if 2 new pairs were injected into the middle of the message, each with value 50? That would increase the sum by 100, but since we only take the low 2 digits, it too would go undetected.

So there are some types of errors that our simple checksum would fail to detect, but it's still pretty good. We know it can't be perfect because it's using just 32 bits (or 2 digits, in the decimal example) to try to guard a message that's potentially much longer. We simply want to choose a checksumming scheme that is *good at detecting the types of errors we expect*, and in exchange we can let it be less good at detecting the types of errors we consider unlikely.

It is in this regard that error-detection checksums differ significantly from other "calculate a fixed-length thing that sort of represents this other data" schemes, like cryptographic hashes. Those are more generally attempting to "sign" (uniquely identify) a message and make it prohibitively difficult to generate a DIFFERENT message that ends up producing the same signature.

In our case, that's not really the goal. We do NOT expect a failure mode in which the message that arrives is COMPLETELY different from the one we sent, i.e. an adversary deliberately attempting to craft a message that "collides", meaning ends up with the same hash. We expect small perturbations caused by imperfect data transfer mechanisms, like single-bit errors, double-bit errors, or burst errors, where several bits in a row are wrong.

That is (part of) why we don't typically use MD5 or SHA-2 or some other familiar hash for this purpose.

So what should we use?

Well, in order to have the desired extremely high probability of detecting these common, expected errors, we will require something that changes a LOT based on even a small change in the input, and also, it must change in a way that's dependent on the POSITION of the corruption - we shouldn't easily be able to undo the perturbation to the checksum by introducing another, similar error elsewhere in the message.

Oh, and it needs to be fast. We want an algorithm that can produce the checksum by just traversing the message, linearly, once - partly for speed, partly because it should be possible to implement this algorithm in a hardware shift register where we compute the checksum on the fly, as the message passes through, and can't store the whole message in order to traverse it more than once.

However, even within linear-time algorithms, there is fast and there is slow. Ideally, we find something that doesn't have to linger for very long on each byte of the message before it's ready to move on to the next one.

There are, it turns out, solutions that satisfy all these constraints.

We ALMOST had the right idea with the simple checksum, where we added blocks of the message together. If we increase the strength of the arithmetic operation, we can do it: from addition, to division.

This is the idea behind a class of algorithms for solving this problem called Cyclic Redundancy Checks, or CRCs. These algorithms view the message M as a single long number, and then divide it by a constant. In doing so, we don't keep the quotient (the result of the division), since that changes slowly. Instead, we take the remainder as our checksum.

Remainders are always strictly less than the thing you're dividing by, so by choosing that constant divisor, we can set the size of the checksum to our desired checksum size.

However, remember how one of the problems with addition is that it's too easy for corruption in one place to counteract corruption in another, such that the checksum fails to change? Division is much better, but bits are still in danger of affecting their neighbors too easily via CARRIES. (Technically BORROWS as it's subtraction, but we can refer to either simply as carries.) When you long-divide, you move along the dividend until you can subtract out (a multiple of) the divisor, right? And when a digit of output exceeds its acceptable range during subtraction, you borrow/carry from the next digit. This pollution across digits can weaken the error-detection.

So what if we just let the digit stay out-of-bounds, and didn't borrow/carry it back into range?

We can do that.

Let's take a step back and look at how numerical representation works.

Each digit of a decimal number is just a weighted contribution to a sum. For example, the number 152 is just:

```
    100
   + 50
   +  2
  -----
    152
```

Each digit contributes itself times a certain power of the base of the number system (10, in this case). In this case:

```
    152 = 1 * 100  + 5 * 10   + 2 * 1
        = 1 * 10^2 + 5 * 10^1 + 2 * 10^0
```

We can generalize the base out entirely, so the number 152 becomes:

```
    1 * x^2 + 5 * x^1 + 2 * x^0 = x^2+5x+2
```

In this general form, we have represented a number, in this case 152, as a POLYNOMIAL in x. All arithmetic rules still apply and will work correctly. For example, we could multiply this polynomial by another polynomial, and after distributing the result, we could substitute any base in for x and arrive at the correct answer for having multiplied two numbers of that base together.

As an example, let's consider the two polynomials:

```
    A = 1 * x^2 + 1 * x^1 + 0 * x^0 = x^2 + x^1
    B = 0 * x^2 + 1 * x^1 + 1 * x^0 = x^1 + 1
```

Let's consider multiplying these two polynomials:

```
    P = (x^2+x^1)(x^1+1) = (x^2+x)(x+1) = x^3+x^2+x^2+x = x^3+2x^2+x
```

As a quick check, let's look at what we've got so far, plugging in 2 for to interpret our inputs and outputs as numbers in base 2 (binary):

```
    A = 6
    B = 3
    P = 2^3+2*2^2+2 = 8+2*4+2=8+8+2=18
```

The polynomial multiplication correctly produced that 6*3=18 when we substitute the base 2 for x.

However, notice that the polynomial P itself ISN'T valid to be directly read off as binary, even though A and B are. Its coefficients are 1, 2, 1, 0, and so if we write out P interpreted as binary, we get 1210. We can't have a coefficient of 2 in binary! Of course, if you actually substitute 2 in for x and then sum, you'd get a CARRY operation that would propagate left, resulting in a proper binary result. In other words, if we had actually done the multiplication in binary:

```
        110
      * 011
      -----
        110
    +  110
    + 000
      -----
```


I'll stop the multiplication here to note that if we added straight down, not worrying about carries, we'd get 1210, which is exactly what we got from the polynomial arithmetic, since with polynomials, you CAN'T carry, as you don't know how coefficients trade off against each other.

But here in binary arithmetic, we know adding 1 and 1 produces 2, which carries over to form 10, so we continue the math:

```
    = 10010
```

Which is 18 in base 10.

So from this we see that the only special thing about approaching an operation as polynomial arithmetic instead of implicitly already including the base is that the base gives you the information you need to propagate carries. With polynomials, you still get the right answer, or you're ready to when you perform the sum, but carries are deferred until you substitute in the correct base.

...or, we could never substitute it, and suppress carrying entirely! This would allow us to never "pollute" a nearby digit, and would therefore be quite good for error detection.

So a good strategy would be to generate a checksum by taking the remainder after a POLYNOMIAL DIVISION.

Unfortunately, allowing each digit to take an arbitrary value is impractical - how would you represent the digit? We've gone a little too far in quality at the expense of practicality.

There is a way to back off the quality just slightly for a dramatic increase in practicality: don't preserve each digit of the result as a completely ARBITRARY value; instead, still don't carry, but limit each digit to a sensible range. Because computers are performing arithmetic in binary, ideally we would restrict it to the range [0, 1], i.e. preserve it MODULO 2.

"Modulo x", or "mod x", just means "the remainder after dividing by x". Therefore "modular arithmetic in base x" is doing math where all operations occur modulo x.

Think of it like 24-hour "clock" arithmetic, which is arithmetic modulo 24. When adding to or subtracting from a time, if an addition takes you above 23 or below 0, you wrap it back into range by subtracting or adding 24 at a time, repeatedly, until you're back in range. Or equivalently, you keep just the remainder after a division by 24.

As an example:

```
    22 + 1 = 23
    23 + 1 = 0 (because we wrap around at 24, i.e. the remainder of 24 / 24 is 0)
     1 - 1 = 0
     0 - 1 = 23
```

If we perform our polynomial division in mod 2 arithmetic, we can avoid carries, yet still keep all our output digits either 0 or 1, i.e. bits. Since we're programming computers, our input and output numbers are already in base 2. This means our output can also be in base 2. Perfect!

So to recap: we will produce our checksum by performing polynomial division mod 2 on the message, dividing it by a constant.

Now let's figure out how to implement "polynomial division mod 2".

Basic arithmetic operations first.

Polynomial addition mod 2:
We have no carries (because it's polynomial arithmetic), so each pair of bits can be added independently. The four possibilities are:

```
      0         1         0         1
    + 0       + 0       + 1       + 1
    ---       ---       ---       ---
      0         1         1         2
```

But because we're mod 2, the result of (1 + 1 = 10 in binary = 2 in decimal) will actually wrap back to 0, so the real answers are:

```
      0         1         0         1
    + 0       + 0       + 1       + 1
    ---       ---       ---       ---
      0         1         1         0
```

Note a very interesting thing about this result. If we had done 1 + 1 in binary, we'd have gotten 10, i.e. 0 with a carry of 1. If we discard the carry, we get 0. In other words, we don't actually have to do any modulo (division) operations to get our answers. We just have to throw away the carry after a normal binary addition, i.e. perform "carryless" binary addition.

Note another interesting thing: the answer was 1 if exactly one input was 1. If neither or both inputs were 1, the answer was 0. This is equivalent to an XOR (exclusive OR) operation.

So, combining these observations:

polynomial addition mod 2
    is equivalent to
carryless binary addition
    is equivalent to
XOR.


Moving on to polynomial subtraction mod 2:

```
      0         1         0         1
    - 0       - 0       - 1       - 1
    ---       ---       ---       ---
      0         1        -1         0
```

But because we're mod 2, the result of 0 - 1 will actually wrap back to 1, so the real answers are:

```
      0         1         0         1
    - 0       - 0       - 1       - 1
    ---       ---       ---       ---
      0         1         1         0
```

WAIT - that's the same set of answers as polynomial addition mod 2! So it's just XOR again! And a quick check to confirm that it's also the same as binary subtraction with no carries: 0 - 1 = 1 with a borrow of 1. If we discard the borrow/carry, we get 1. Yep!

So this means:

polynomial addition mod 2
    is equivalent to
carryless binary addition
    is equivalent to
polynomial subtraction mod 2
    is equivalent to
carryless binary subtraction
    is equivalent to
XOR.


And because multiplication is just repeated addition, and division is the inverse of multiplication, we see that the entire suite of polynomial arithmetic mod 2 is EQUIVALENT to binary arithmetic with no carries, i.e. carryless binary arithmetic. This is great, because carryless binary arithmetic is very tractable to perform with a computer.

Onto polynomial multiplication mod 2. We'll consider writing out a long multiplication. The operation is quite simple, for three reasons:
1) in binary, as you walk along multiplying the top number by each bit of the bottom number, and adding it all up, you're only ever multiplying by 0 or 1, i.e. either adding 0 or adding the top number itself. No multiplication required.
2) no carries on the subsequent addition
3) the subsequent "addition" is actually just XOR!

Here's an example:

```
         1101
       * 1010
         ----
         0000   first bit of the bottom number is 0, so add nothing.
    +   1101    second bit of the bottom number is 1, so add the top number.
    +  0000     third bit of the bottom number is 0, so add nothing.
    + 1101      fourth bit of the bottom number is 1, so add the top number.
    ---------
      1110010
```

So the result of polynomial multiplication mod 2 is just the XOR'ing together of some shifted-over copies of the top number: one for each set bit position in the bottom number. And of course, you can reverse the top and bottom numbers and you'll get the same answer.

And finally, the one we REALLY care about, since it's how we'll implement our CRC: polynomial division mod 2. We'll consider writing out a long division. Because it's binary, we never need to actually divide anything; as we traverse along the dividend, we just do a subtraction (XOR!) any time the top bit of our divisor matches up with a set bit in the dividend, because that means we can subtract (XOR) our divisor, shifted over to that position, to reduce the size of the remainder. We continue until there's nothing left but a remainder that is strictly less than our divisor:

```
          1101000
         ________
    101 ) 1110001
        - 101
        ---------
          0100001
        -  101
        ---------
           001001
        -    101
        ---------
             0011
```

The quotient is 1101000. The remainder is 11.

As usual, the quotient, multiplied by the divisor, will get us close to the dividend - we'll be off by the remainder. In this arithmetic, the quotient tells us what shiftings of the divisor we should XOR together to get as close as possible to the dividend without going over. If we then add (XOR) the remainder, we'll recover the dividend exactly.

So... did we just calculate a CRC?

ALMOST. Almost.

Notice that to perform the division above, we started at the top bit of the dividend, and marched along to the right. Call the size of the divisor, in bits, n. Anytime we encountered a set bit, we XOR'd that location by the divisor (shifted down by n - 1). And we repeated this process, all the way to... NOT the entire dividend! We stop n - 1 bits early, because past that point, the portion of the dividend we are considering is smaller than the divisor. There is no longer any possibility of the divisor dividing into it, so we're just left with the remainder, which we drop straight down and call it a day.

But why should these last n - 1 bits fail to get the treatment the rest of the bits did? There's nothing special about them, and they too should go through the same "CRC machine" that the rest did if we want a quality checksum. You can prove this mathematically, but I like this intuitive description of the situation: all bits should go through the grinder in succession.

So, we pad the dividend (the message!) with an extra n - 1 zero bits at the end, then perform our carryless binary divide (== polynomial divide mod 2), and THAT's a CRC.

It's typical with CRC to call the divisor constant the "polynomial", or "poly" for short. It is polynomial division, after all.

Note something cool: having chosen a poly that is n bits long, we can be CERTAIN that our checksum (remainder!) will be n - 1 bits long (or fewer). This is NOT true in normal division; consider the remainder after division by 17, a two-digit divisor. The remainder after division could be 11, or 15, or 16 - all also two digits in length.

But it IS true in polynomial division mod 2 a.k.a. carryless binary division. Recall that in this sort of division, we walk along the dividend and XOR in the divisor anytime the top bit of the divisor matches up with a set bit in the dividend, in order to "subtract" (turn off) that bit. We continue down the line, turning off bits, until we fall off the end. If the poly is of size n, the last bit we will turn off is therefore the nth bit - leaving UP TO N - 1 BITS in the remainder. So we can guarantee a checksum of size n - 1 bits (or fewer) for an n-bit poly.

This also means that to perform the division, we can maintain an n - 1 bit buffer, called a "register", which we initialize to 0. We do all our XOR'ing against this buffer, which is storing our intermediate remainder, until we're done, at which point it contains the final answer, up to n - 1 bits long.

For this reason, it is common to define the WIDTH, w, of a poly:

```
    w = n - 1
```

So a poly that is n == 33, i.e. it's 33 bits long, i.e. its highest set bit is its 33rd, we say has w == 32. We call it a poly of width 32. The resulting checksum will be 32 bits. And the register we will perform the computation in need only be 32 bits large.

Because modern computers generally operate internally on register sizes of 8, 16, 32, or 64 bits, it is common to select polys of width 8, 16, or 32, i.e. polys of 9, 17, or 33 bits, respectively.

64-bit CRCs (so a 65-bit poly) do exist but are rarely used because 32-bit CRCs are "good enough" - they have impressively good error detection for messages of any reasonable length, and it's just not necessary to go longer (and slower).

We now know our implementation goal: compute the remainder after carryless binary division of an arbitrary-length message padded with w zero bits, divided by an n-bit poly (i.e. width w), where w is a convenient computer register width, so 8, 16, or 32.

Mathematically, then, we have:

```
    CRC(M) = (M << w) mod P == (M * (1 << w)) mod P
```

```
 - P is our poly
 - "<< x" is a shift to the left by x bits, which is equivalent to a multiplication by (2 to the xth power)
 - mod x means divide by x and take the remainder, i.e. modulo x, although here that means carryless binary division
 - * is just multiplication, although here that means carryless binary multiplication.
```

For the mod operation, we can't just use the divide instruction of our processors, because that's normal binary division, not carryless. We must implement our own!

This can be done relatively efficiently in hardware with a custom logic circuit, but here, we're interested in implementing it on a CPU. I will be focusing on the modern x86 CPU architecture, which is found in the PS4, the Xbox One, and nearly all home and business computers.

The choice of what polynomial P to use is critical to producing a CRC algorithm with good error-detection properties. It's possible to mathematically prove robustness against various classes of errors for a given polynomial, and the community has converged on a number of popular polynomials with good properties for each common CRC size. Deriving and proving the quality of a polynomial is very tricky, so it's HIGHLY recommended to use one of these rather than trying to derive your own.

Okay, time to implement our first, naive, algorithm, following our example of carryless binary long division and describing it as a procedure to follow:

```
 - start at the first bit of the message. consider this the "current bit".
 - until every bit in the message has been the "current bit":
     - check the current bit. if it is set, XOR the n bits of message starting at the current bit with the polynomial. this will turn off the current bit.
     - advance the current bit forward by 1 bit
```

Now the message has become a long string of 0s (since we marched along turning bits off) followed by a region of (up to) w bits that can contain 1s. This is our result.

Note that we will have to append zero bits to the message, as previously discussed, so that as our current bit nears the end of the message, we still have something to XOR the poly against. We can also verify how many bits we will need now: if the last current bit will be, well, the last bit, and we will need to XOR against the poly, starting from that position, and the poly is length n bits, we will need n - 1 = w bits of extra zeros at the end of the message.

We are now ready to attempt a direct mapping of this algorithm into Python code. It should be fairly straightforward. Let's assign some variables:

```
- P            our poly P
- n            the number of bits in the poly
- w            the width of the poly, which is n - 1
- M            the message M
- len_m        the number of bits in the message
```

Let's assume P and M are stored as lists of integers, where each integer represents a binary digit and is thus either 0 or 1. This is not efficient, but it's good for prototyping and keeps things simple.

And here's our code:

```
    M += [0] * w                    # append w zero bits so we don't fall off
    for i in range(len_m):          # for each bit in M:
        if M[i]:                    #  if bit is set:
            for j in range(n):      #   for each bit from current to end of poly:
                M[i + j] ^= P[j]    #    XOR message bit with poly bit
```

Not too bad, right? We append our 0 bits, then traverse the message. At each current bit, if it's set, we xor the poly into the message at that point.

Let's try to run this code with an example poly and message. To keep things short let's use an 8-bit poly (i.e. n = 9, w = 8):

```
    P = 0b100000111
    n = 9
    w = 8
    M = 10101010001001001111100000100011
    len_m = 32
```

The code runs, performs the long division, and after it's done, M has been replaced with the division remainder:

```
    Before CRC, after adding padding 0 bits:
    M = 1010101000100100111110000010001100000000

    After CRC:
    M = 0000000000000000000000000000000011011110
```

The message has been zeroed out, as we would expect from calculating a remainder, except for the last 8 (= w) bits, which contain our remainder.

And we're done! This is our CRC. The CRC of that message, with that polynomial, is 0b11011110.

But we're just getting started. Let's look at some improvements.

The first is that we should do our work into an intermediate working register, R, of size w, rather than XOR'ing into the message itself. This is because we don't want to destroy the message! Or have to make a copy of it just to be able to CRC it. So we shouldn't modify M itself. There is also potentially some efficiency to be gained by performing all operations into a single intermediate register until it contains the final answer, rather than modifying the entire message.

At each step, we KNOW the current bit will end up off. It's either off to start with, or we TURN it off when we perform our XOR. This is why we only need a w-bit working register, despite the poly being n = w + 1 bits long. At every moment, we only care about the w bits starting just past the current bit.

In other words, the working register's "view" of the message begins one past the current bit. Think of it as a sliding window that sweeps to the right across the message until all bits have been visited, i.e. until R's top bit is one bit past the end of the message.

At each step, we check the current bit, which just slid out of the left side of R. If it's set, i.e. if we shifted out a 1 bit, we XOR R against the poly. We the shift the sliding window to the right, or equivalently, shift its contents one bit to the left, including shifting in the next bit of message into the newly vacated rightmost bit.

Note that instead of having to separately "catch" and check the leftmost bit at each iteration, after it has been shifted out of the register, to decide whether to XOR on this step, we can simply check it first, THEN shift it out.

This also means we can drop the (always set) high leftmost bit of the poly, and just keep the remaining w bits. From now on, P will be represented this way, as the truncated w-bit version instead of the n-bit version with a leading 1.

```
    M += [0] * w                     # append w zero bits so we don't fall off
    R = M[:w]                        # initialize R with the first w bits of M
    for i in range(len_m):           # for each bit in M:
        leftmost_bit_set = R[0] == 1 #  check whether we're about to shift out a 1
        R = R[1:] + [M[i+w]]         #  shift left, and shift in a new M bit
        if leftmost_bit_set:         #  if we shifted out a 1:
            for j in range(w):       #   for each bit in poly:
                R[j] ^= P[j]         #    XOR R bit with poly bit
```

Okay! This version successfully uses a w-bit working register R to store the intermediate result and eventually the final answer.

However, this version still has the problem that it must let the window go off the end of M by w bits. Because we shift in a new message bit every iteration, we have to either append w zero bits to M (as I do here) or have a special-case condition for those last few bits. Neither of these are great options.

Fortunately, there is an elegant way to solve the problem: we recognize it is not actually necessary to shift new message bits in right away as we traverse. Consider what happens to these bits in the current example. They are shifted in from the right, one at a time.

Then, the poly is XOR'd into R, or not, depending on whether a 1-bit was shifted out the left side.

R shifts again.

Poly is possibly XOR'd again.

This process repeats, with our new message bit slowly moving to the left and getting XOR'd against some number of times, until it finally is the leftmost bit. We shift it out and use it to decide whether to XOR again.

In other words, the value of the bit once it reaches the leftmost position is:

```
    M[i] ^ a ^ b ^ c ^ d ^ ...
```

Where ^ is XOR and a, b, ... are whatever bits we end up XOR'ing into it as it travels along, if any.

But this is equivalent to:

```
    0 ^ M[i] ^ a ^ b ^ c ^ d ^ ...
```

And XOR is commutative! So we can just as well write:

```
    0 ^ a ^ b ^ c ^ d ^ ... ^ M[i]
```

In other words, we can just shift in 0 bits into the right side of R as we traverse. Instead of inserting M bits into the right side, we can XOR them into the leftmost bit, just before they are to pop out!

```
    R = [0] * w                      # initialize R with all 0s
    for i in range(len_m):           # for each bit in M:
        R[0] ^= M[i]                 #  XOR it into the leftmost bit
        leftmost_bit_set = R[0] == 1 #  check whether we're about to shift out a 1
        R = R[1:] + [0]              #  shift left, and shift in a 0 bit
        if leftmost_bit_set:         #  if we shifted out a 1:
            for j in range(w):       #   for each bit in poly:
                R[j] ^= P[j]         #    XOR R bit with poly bit
```

Now that's more like it. Very clean. We don't modify M, we don't append zeros or anything. We don't have to special-case anything. We can initialize R with 0s since message bits go in directly on the left. It's perfect!

Well, almost.

This approach shifts in a single message bit per iteration. Unfortunately, real CPU hardware addresses BYTES (8 bits) at a time, so although we could implement this as is, a small modification to XOR in a whole byte at a time will improve its suitability for real hardware:

```
    R = [0] * w                          # initialize R with all 0s
    for i in range(len_m // 8):          # for each BYTE in M:
        for j in range(8):               #  XOR it into the leftmost BYTE
            R[j] ^= M[8*i + j]
        for j in range(8):               #  process 8 BITS:
            leftmost_bit_set = R[0] == 1 #   check whether we're about to shift out a 1
            R = R[1:] + [0]              #   shift left, and shift in a 0 bit
            if leftmost_bit_set:         #   if we shifted out a 1:
                for k in range(w):       #    for each bit in poly:
                    R[k] ^= P[k]         #     XOR R bit with poly bit
```

This is exactly the same algorithm, except that we process a batch of 8 bits at a time, instead of 1 at a time. We XOR a whole byte into the left side of R, then perform 8 iterations of shift-and-possibly-XOR, then repeat.

Naturally, this restricts our message size to a multiple of 8 bits (1 byte). This is not usually a troublesome limitation on CPU hardware, because data generally IS thrown around in multiples of bytes already.

Okay, let's ground this a little bit by deciding on a fixed size for w - how about w = 8 for now - and replacing some of our lists of bits with actual numbers, integers, stored directly as Python integer variables. R will be an 8-bit integer variable. P will be an 8-bit integer constant. M will be a list of BYTES. len_m will be the number of bytes in M. Same algorithm:

```
    R = 0                               # initialize R with 0
    for i in range(len_m):              # for each BYTE in M:
        R ^= M[i]                       #  XOR it into the leftmost/only byte of R
        for j in range(8):              #  process 8 BITS:
            leftmost_bit_set = R & 0x80 #   check whether we're about to shift out a 1
            R = (R << 1) & 0xFF         #   shift left, and shift in a 0 bit
            if leftmost_bit_set:        #   if we shifted out a 1:
                R ^= P                  #    XOR P into R
```

In some ways it's now simpler. We no longer have to iterate over each of w bits in order to XOR one bit at a time - instead we can just XOR two integers together directly, and their bits will XOR together in parallel.

Note that we must manually restrict R to 8 bits (the "& 0xFF" step) as integer variables in Python do not have a fixed size and will otherwise continue to grow beyond 8 bits as we left-shift. We won't have to do this in an actual 8-bit CPU register.

Note also that XORing the new message byte, M[i], into the leftmost byte of R, is trivial for now, because R itself is only 1 byte long, so we can just XOR M[i] directly into R.

So let's now rewrite this with w = 32, as a 32-bit CRC is the eventual goal of this paper. Once we do so, we will no longer be able to get away with XORing M[i] directly into R, and will actually have to XOR it into the leftmost byte of R:

```
    R = 0                                    # initialize R with 0
    for i in range(len_m):                   # for each BYTE in M:
        R ^= M[i] << 24                      #  XOR it into the leftmost byte of R
        for j in range(8):                   #  process 8 BITS:
            leftmost_bit_set = R & (1 << 31) #   check whether shifting out a 1
            R = (R << 1) & ((1 << 32) - 1)   #   shift left, and shift in a 0 bit
            if leftmost_bit_set:             #   if we shifted out a 1:
                R ^= P                       #    XOR P into R
```

Same idea, but we have to shift each incoming message byte M[i] left by 24 bits (i.e. 3 bytes) so that it can be XOR'd into the leftmost byte of R:

```
    R               [x x x x]
    M[i]            [0 0 0 x]
    M[i] << 24      [x 0 0 0]
```

But we can avoid shifting this if we just reverse the bit order - "reflect" - our R register. We're currently feeding in bits from the right, and shifting R to the left. But that was only chosen because it matches our intuition about the "sliding window" used in long division. Now that we understand the process, there's no reason we can't just reverse the bit order, and shift in bits from the left, i.e. shifting R to the right. Then, we'll be able to XOR the message into the RIGHTmost byte, which requires no shifting.

This WILL change the output of the CRC, but it does not make it any weaker, nor stronger. It's the equivalent algorithm, just bit-reflected. As long as sender and receiver both do it this way, it remains a valid approach to error detection.

Be warned that some implementations do this and some don't! To make it even more confusing, some reflect just the input, or just the output! These are all valid approaches to error detection, as long as sender and receiver agree, but it can certainly make it confusing to try to match or understand someone else's implementation.

Here's what our algorithm looks like with a reflected R:

```
    R = 0                             # initialize R with 0
    for i in range(len_m):            # for each BYTE in M:
        R ^= M[i]                     #  XOR it into the rightmost byte of R
        for j in range(8):            #  process 8 BITS:
            rightmost_bit_set = R & 1 #   check whether we're about to shift out a 1
            R >>= 1                   #   shift right, and shift in a 0 bit
            if rightmost_bit_set:     #   if we shifted out a 1:
                R ^= P                #    XOR P into R
```

Very nice. No shifting of the incoming message bytes, and a simpler expression for checking whether we have to XOR in P at each iteration as we check the rightmost bit now. This is looking very simplified and optimal, for the naive approach! We're ready to get serious, and move this to x86 CPU instructions (a.k.a. x86 assembly) so we can start timing it and experimenting with incremental improvements to performance. Yes, assembly. I know it sounds scary. It's not. I promise! We will go over it slowly. And we'll do this exact same algorithm that you see above in Python.

The outer structure is fairly simple: zero out R, loop over all the bytes in M. For each one, XOR it into R, then perform the inner loop 8 times. Since we know we always execute the inner loop's contents exactly 8 times, we should just write the corresponding x86 CPU instructions out 8 times explicitly, instead of wasting time incrementing some counter from 0 to 8, checking it after every iteration, and jumping back up to repeat. This is known as LOOP UNROLLING, and it provides a nice speed improvement. An example of a loop vs. an unrolled loop in x86:


Using a loop:

```
    mov cl, 8        ; store 8 into the register "cl"
    LOOP_TOP:        ; an arbitrarily-named label marking the top of the loop
    ; do stuff       ; the actual instructions that are the loop contents would go here
    dec cl           ; decrement cl (decrease it by 1)
    jnz LOOP_TOP     ; if the result of the dec was not zero, jump back to LOOP_TOP
```

Using loop unrolling:

```
    ; do stuff
    ; do stuff
    ; do stuff
    ; do stuff
    ; do stuff
    ; do stuff
    ; do stuff
    ; do stuff
```

Unrolling a loop very many times, or unrolling a loop with very many iterations, has implications for code size and possibly branch prediction (since any branches contained within will be considered and predicted separately instead of being recognized as the same branch). But for a small, simple loop like ours, repeated just 8 times, unrolling is definitely the way to go.

Now we must find the best way to implement the "do stuff" part: the actual contents of the inner loop.

We have some x86 register that we are using as R, and we have our 32-bit integer constant P. We are to implement the following:

 - shift R right by 1
 - if we shifted out a 1 bit, XOR P into R, otherwise do nothing

I can think of 4 ways to implement this.

Before we begin, a reminder that code implementations for all of the options accompany this document.


### OPTION 1: Check Carry Flag and Jump ###
This option implements the above pseudocode steps almost directly. In x86 CPU instructions:

```
    shr R, 1        ; shift R right by 1
    jnc SKIP_XOR    ; if the carry flag is not set, jump to SKIP_XOR
    xor R, P
    SKIP_XOR:
```

In practice 'R' would be replaced by some named 32-bit x86 register that we were using to represent R, like eax or esi, but I'll just use 'R' directly in these examples to keep things simple.

The 'shr' instruction shifts R right by 1. The bit that is shifted out is placed in the CARRY flag of the processor. This allows us to check whether we shifted out a 1 by looking at the state of the carry flag. If the carry flag is not set after the shift, we shifted out a 0, so we jump to the label SKIP_XOR, skipping over the XOR operation and thereby not performing it unless we shifted out a 1.

This is simple and compact, but it's slow due to branch misprediction.

Modern CPUs have long pipelines that can't afford to wait at a branch until they know for sure which way it will go, so they predict as best they can and then forge ahead, hoping they predicted correctly. If they did not, they have to discard that work and restart back at the branch point, taking the other branch. Therefore, mispredictions are quite expensive.

With random input data, the branch predictor, attempting to determine whether we will take that 'jnc' conditional jump, will not be able to improve upon the prediction rate you'd get by guessing randomly, which is 50%. We will therefore mispredict on half our bits, wasting a lot of cycles.

On random data, on a high-end modern x86 CPU, at the time of writing, each iteration of this loop will execute in about 11 cycles on average. This WILL vary a lot depending on the data. Either way, that's about 11 cycles per bit. Ouch.


### OPTION 2: Multiply Mask ###

This option saves off the rightmost bit prior to shifting. We can then multiply that bit, which will be either 0 or 1, by P, resulting in either 0, if the bit was 0, or P, if the bit was 1. We then XOR that into R, resulting in either R ^ 0 = R, if the bit was 0, or R ^ P, if the bit was 1, as desired.

In pseudocode:

```
    tmp = R        ; make a temp copy of R prior to shifting
    R >>= 1        ; shift R
    tmp &= 1       ; keep only the rightmost bit of tmp, producing either 0 or 1
    tmp *= P       ; multiply tmp by P, producing either 0 or P
    R ^= tmp       ; XOR tmp into R, producing either R or R ^ P
```

In x86:

```
    mov tmp, R        ; cycle 1
    shr R, 1          ; cycle 1
    and tmp, 1        ; cycle 1
    imul tmp, P       ; cycles 2, 3, 4
    xor R, tmp        ; cycle 5
```

This option avoids a branch, but it has an integer multiply, which isn't as cheap as we'd like. On modern hardware we can expect 5 cycles per bit.

Note that some independent instructions are capable of executing simultaneously on modern hardware, which is why the first three instructions in the example will all complete in the first cycle (technically, the mov will be eliminated during the register-rename stage of the pipeline and won't execute at all, and then the next two, independent, instructions will execute simultaneously).

Note also that because the output of one iteration of the loop (R) is then the input into the first step of the next iteration, we have a loop-carried data dependency, so we won't be able to start executing instructions from the next iteration before the current one has finished. This means our simple single-iteration cycle count of 5 should be representative.

Although 5 cycles per bit is an improvement over 11, we can still do better.


### OPTION 3: Bit Mask ###

This option also saves off the rightmost bit prior to shifting, but rather than multiplying it by P to produce either 0 or P, we negate it, producing either 0 or -1, then bitwise AND it by P, producing either 0 or P. This works because -1 is represented as all 1 bits (negative integers are represented using two's complement in x86, so -1 means "what value could we add 1 to to produce 0?". The answer is all 1 bits, since adding 1 would produce 0 via modular arithmetic - we'd overflow the register and it would roll back to all 0 bits.).

In pseudocode:

```
    tmp = R        ; make a temp copy of R prior to shifting
    R >>= 1        ; shift R
    tmp &= 1       ; keep only the rightmost bit of tmp, producing either 0 or 1
    tmp = -tmp     ; negate tmp, producing either 0 or -1
    tmp &= P       ; bitwise AND P into tmp, producing either 0 or P
    R ^= tmp       ; XOR tmp into R, producing either R or R ^ P
```

In x86:

```
    mov tmp, R        ; cycle 1
    shr R, 1          ; cycle 1
    and tmp, 1        ; cycle 1
    neg tmp           ; cycle 2
    and tmp, P        ; cycle 3
    xor R, tmp        ; cycle 4
```

This option avoids branching and multiplying, so it's pretty good. 4 cycles per bit.

In fact, most sources will tell you it's optimal even on modern hardware.

It's not.

We can still do much better.


### OPTION 4: Conditional Move ###

x86 has a family of conditional move instructions, which copy data from one register into another, but only if a certain condition (involving flag bits) is true. We can use this to our advantage.

In this option, we prepare a mask register to XOR into R. We initialize it to 0, then conditionally move P into it based on the carry flag, which will be populated by the bit that is shifted out of R:

In pseudocode:

```
    tmp = 0
    R >>= 1
    if we shifted out a 1, tmp = P
    R ^= tmp
```

In x86:

```
    xor tmp, tmp    ; cycle 1
    shr R, 1        ; cycle 1
    cmovc tmp, P    ; cycle 2
    xor R, tmp      ; cycle 3
```

Note that we zero the tmp register by XORing it against itself.

3 cycles per bit! As far as I am aware, this is optimal for the naive algorithm.


The next question is, can we write something in C/C++ which will convince a compiler to generate Option 4? That would be preferable to having to maintain an assembly version, especially as we begin to experiment with more complex approaches to further improve performance.

Well, the fact that we perform a conditional move based on a flag set by the shift itself is not a good sign: at the time of writing, C/C++ do not provide a means to express this idea directly, and compilers are still very poor at detecting and generating it automatically.

The best solution I know of is actually just a direct C port of the Python solution:

```
    uint32_t crc32(const uint8_t* M, uint32_t len_m) {
        uint32_t R = 0;                         // initialize R with 0
        for (uint32_t i = 0; i < len_m; ++i) {  // for each BYTE in M:
            R ^= M[i];                          //  XOR it into the rightmost byte of R
            for (uint32_t j = 0; j < 8; ++j) {  //  process 8 BITS:
                bool rightmost_bit_set = R & 1; //   check whether shifting out a 1
                R >>= 1;                        //   shift right
                if (rightmost_bit_set)          //   if we shifted out a 1:
                    R ^= P;                     //    XOR P into R
            }
        }
        return R;
    }
```

I included the complete function so that the type of M and the return type of the function are visible: M is an array of byte data, similar to the "list of bytes" representation in Python. (Technically, it's a pointer to the start of such an array.) uint32_t is an unsigned 32-bit integer type.

We can also write this a bit more compactly using a C++ ternary, which looks like this:

```
    condition ? a : b
```

The value of the expression is 'a' if the condition is true, or 'b' if not.

This gives:

```
    uint32_t crc32(const uint8_t* M, uint32_t len_m) {
        uint32_t R = 0;                         // initialize R with 0
        for (uint32_t i = 0; i < len_m; ++i) {  // for each BYTE in M:
            R ^= M[i];                          //  XOR it into the rightmost byte of R
            for (uint32_t j = 0; j < 8; ++j) {  //  process 8 BITS
                R = R & 1 ? (R >> 1) ^ P : R >> 1;
            }
        }
        return R;
    }
```

At the time of writing, all major modern compilers produce the following code generation, or equivalent, for either of these inner loop implementations:


### OPTION 5: Compiler Output ###

In pseudocode:

```
    tmp = R                              ; create a temp copy of R
    R >>= 1                              ; shift R
    tmp2 = R                             ; create another temp copy of R
    tmp2 ^= P                            ; tmp2 is now R ^ P
    if we shifted out a 1, R = tmp2
```

In x86:

```
    mov     tmp, R      ; cycle 1
    shr     R, 1        ; cycle 1
    mov     tmp2, R     ; cycle 2
    xor     tmp2, P     ; cycle 2
    test    tmp, 1      ; cycle 2
    cmovnz  R, tmp2     ; cycle 3
```

This approach also uses a conditional move, but rather than using the result of the shift to directly prepare a mask (either 0 or P) to XOR into R, we shift R and unconditionally compute R ^ P, then do an explicit check of the rightmost bit of the original unshifted R, and conditionally move our R ^ P into R if we shifted out a 1 bit.

The fact that this approach requires two temporary registers is not ideal. It also consists of more instructions, larger code size, and will perform worse on older architectures.

However, on high-end modern architectures, it, too, will complete in 3 cycles, tied with option 4.


So 3 cycles per bit seems optimal, and in some sense, it is. If you operate 1 bit at a time, you will not be able to avoid spending at least 3 cycles per iteration.

The key to unlocking higher performance is to find ways to process more than 1 bit at a time.

To accomplish this, we recall that each iteration of the outer loop of our current implementation does the following:

    - XOR the next message byte, M[i], into R
    - perform 8 iterations of the inner loop, each of which
          either XORs P into R or not, depending on what was shifted out

Therefore, before starting the outer loop iteration, we have R. After, we have R shifted right by a total of 8 bits, and with some shiftings of P XOR'd in. The important realization is that WHICH shiftings of P are XOR'd in is entirely and uniquely determined by the rightmost 8 bits of R at the start of the iteration, and by P:

I will use A, B, C, D, ... as placeholders to mean either 0 or P. The effect of a single outer loop iteration, in which we process 1 byte of message, is:

```
    R = (R >> 1) ^ A
    R = (R >> 1) ^ B
    R = (R >> 1) ^ C
    R = (R >> 1) ^ D
    R = (R >> 1) ^ E
    R = (R >> 1) ^ F
    R = (R >> 1) ^ G
    R = (R >> 1) ^ H
```

Turning this into a single expression:

```
    (((((((((((((((R >> 1) ^ A) >> 1) ^ B) >> 1) ^ C) >> 1) ^ D) >> 1) ^ E) >> 1) ^ F) >> 1) ^ G) >> 1) ^ H
```

Then we can distribute the shifts in order to simplify the expression:

```
    (R >> 8) ^ (A >> 7) ^ (B >> 6) ^ (C >> 5) ^ (D >> 4) ^ (E >> 3) ^ (F >> 2) ^ (G >> 1) ^ H
```

We can then combine all the terms containing A through H into a single value T:

```
    (R >> 8) ^ T
```

Which, as we established, is uniquely identified by the rightmost 8 bits of R, and P.

So for a given P, we can precompute and store off a TABLE of T values - the necessary shiftings to XOR in - for each of the 256 possible states of the rightmost 8 bits, then process the whole outer loop iteration at once instead of 1 bit at a time by just looking up the correct precomputed value to XOR in using the rightmost 8 bits, shifting R right by 8, and then XORing the precomputed value in.

The ith entry in the table - the T for rightmost 8 bits of R equal to i - is therefore the XORs, if any, produced by putting the rightmost bits of R through 8 iterations of the inner loop, i.e. 1 iteration of the outer loop, i.e. the CRC of the rightmost 8 bits of R.

In other words, the ith entry in the table, which we'll call tbl[i], is simply CRC(i) for the given P! For any 8-bit value X, we therefore have a way to instantly lookup the CRC:

```
    CRC(X) = tbl[X]     for any 8-bit X
```

Okay, so we precompute and store off our table. Then the new tabular CRC implementation is very simple, and the compiler does do no worse (well, barely worse) than a hand-written assembly implementation. So here it is in C++:


### OPTION 6: 1-byte Tabular ###

```
    uint32_t crc32(const uint8_t* M, uint32_t len_m) {
        uint32_t R = 0;
        for (uint32_t i = 0; i < len_m; ++i) {
            R = (R >> 8) ^ tbl[(R ^ M[i]) & 0xFF];
        }
        return R;
    }
```

Very simple and clean. We ONLY have our outer loop now, each iteration of which processes 1 new byte of message, i.e. 8 bits. We XOR the new message byte into R, use the low 8 bits of the result of that to index into our table and look up our T, and then XOR that into R shifted right by 8.

Our implementation now involves memory access into both the message and the table, and is becoming less compute-intensive and more memory-intensive, which means a measure of its performance in terms of cycles per bit will start to become more variable depending on your particular machine. That said, this approach will typically achieve roughly 0.9 cycles per bit, or roughly 1.1 bits per cycle. As we get faster from here on, I'll start inverting the measurement, so bits per cycle instead of cycles per bit, to keep the numbers greater than 1 and therefore easier to reason about.

So this method is at least a 3-fold increase in speed on the best naive approach, in addition to being much simpler to write out in C++. In exchange, it requires 256 table entries of 32 bits (4 bytes) each, i.e. a table consuming 256*4==1024 bytes of space (1 KB). This is generally an acceptable tradeoff, even in most embedded system implementations, and just as with Option 3, many sources will stop here and tell you this is optimal.


Nope. For some embedded architectures, this option may be the best tradeoff if memory is scarce. But on x86, we can still do MUCH better.


To do so, we can build off the table idea to process more data more efficiently. Currently, for each byte of message, we have to do 3 dependent basic computational operations and 2 memory accesses. We need to reduce this to go faster.

One avenue to consider is simply taking larger chunks at a time. We could process 16 bits instead of 8. We'd shift by 16, not 8, and index into our table using the low 16 bits, not 8. This would mean 2^16 table entries instead of 2^8, which is 65536. That's a 256 KB table - large, but doable.

This would seem like a good improvement - now, for each TWO bytes of message, we do 3 basic computational operations and 2 memory accesses.

Unfortunately, this option is actually slightly SLOWER than Option 5, in addition to using much more memory to store the table.

To understand why, we have to talk about how memory works in modern computers.

As CPUs got faster, memory did too, but not by nearly as much. Modern CPUs cycle WAY faster than memory can supply requested data. It's not uncommon for a memory access to take several HUNDRED cycles. This is true no matter the architecture - low-end laptop, top-of-the-line desktop, or PS4. (Especially the PS4, as its memory is trades high throughput for high latency...)

This disparity between how much work the CPU can perform on data and how long it has to wait to GET the data it needs is so large that CPU designers implement a system of CACHE memory that sits between the CPU and main memory.

The cache is a small bank of memory with lower latency (less time between request and receipt of data) than main memory. It is automatically populated with data from main memory that is predicted likely to be requested by the CPU soon.

For example, if you access the first element of an array, and the array is not in cache ("cold", or a "cache miss"), that access will stall and be very slow as it must arrive all the way from main memory. However, in the process, that element, as well as surrounding elements, will arrive at and remain in the cache. Then, if the next access is located nearby, it will be in the cache ("hot", or a "cache hit") and will arrive at the CPU quickly.

In addition to this caching of nearby data, if you access in a predictable pattern - such as accessing element 0, then 1, then 2, and so on - then additional prefetching will occur to ensure that as long as you follow the sequence, the next element is likely to be present in the cache by the time you need it.

This means we don't have to worry about the accesses to the message in our CRC implementation. They are sequential and predictable. We need to worry about the table accesses, which, from the memory controller's point of view, are unpredictable. Once an element is accessed, it will be brought into the cache, as well as some surrounding elements. But if the cache fills up, elements will be evicted to make room for new ones.

This means our algorithm's performance is acutely sensitive to the size of the cache. If it's not large enough to comfortably hold the entire table, performance will suffer.

In reality, modern architectures actually have multiple levels of caches - typically 2 or 3. Each level is larger, but slower, until the largest and slowest of all - main memory itself.

The smallest, fastest level is the first - Level 1 cache, or L1 cache for short. Typical read access is just 3 cycles or so.

The size of this cache varies, but at the time of writing, around 32 KB is typical.

This means L1 can EASILY hold our entire table if we use 8-bit indices, as it's only 1 KB, but it can't even come close to holding our 16-bit index table, which is 256 KB.

This explains why the 16-bit table is not only much larger, but also slower - so much slower that it outweighs the benefit of fewer total accesses: reads from the table will frequently miss L1 cache.

So we must search for a way to go faster without significantly increasing table size. The realization we will need for this is that CRC is a linear operation. Recall our mathematical definition of the CRC operation:

```
    CRC(M) = (M << w) mod P
```

Consider CRCing two messages XOR'd together:

```
      CRC(X ^ Y)
    = ((X ^ Y) << w) mod P
    = ((X << w) ^ (Y << w)) mod P
    = ((X << w) mod P) ^ ((Y << w) mod P)
    = CRC(X) ^ CRC(Y)
```

This is a big deal, because if our two initial messages X and Y don't overlap, XORing can combine them:

Let
```
    X = xxxxxxxx00000000
    Y = 00000000yyyyyyyy
```

Then
```
    X ^ Y = xxxxxxxxyyyyyyyy
```

To see how this can be useful:

Let A be an 8-bit message: A = aaaaaaaa
Let B be another 8-bit message: B = bbbbbbbb
Let M be the concatenation of the two, i.e. M = aaaaaaaabbbbbbbb = AB
Let z be an 8-bit message consisting of all zeros: z = 00000000

Now we can say:

```
    CRC(M) = CRC(AB) = CRC(Az ^ zB) = CRC(Az) ^ CRC(zB)
```

But leading zeros don't affect the outcome of a CRC, because the value of the message remains the same whether you write mmmmmmmm or 00000000mmmmmmmm:

```
    CRC(zB) = CRC(B)
```

TRAILING zeros do, so we can't perform the same simplification on the A part. Therefore we have:

```
    CRC(M) = CRC(Az) ^ CRC(B)
```

We are going to try to use this property to process a 16-bit chunk of the message instead of just 8 bits at a time. So our A and B will be 8 bits each. We already have our table which can be used to lookup CRC(B) directly. However, CRC(Az) is the CRC of an 8-bit value followed by 8 zero bits:

```
    CRC(Az) = CRC(aaaaaaaa00000000)
```

Our table can't directly give us this, but because the low 8 bits are ALWAYS zero, this 16-bit value nonetheless only has 2^8 = 256 possible states! So we can create ANOTHER table of the same size (256 entries, 32 bits each, i.e. a 1 KB table) that allows us to lookup CRC(Az) for any A.

Note that this is the key distinction that allows us to perform our 16 bits of CRC without a 16-bit table: having using the linearity of CRC to separate the 16 bits into two 8-bit chunks, we can use 2 8-bit tables instead of 1 16-bit table, which is the difference between 2*(2 to the 8th) entries and 1*(2 to the 16th) entries, which is 512 entries instead of 65536.

If we call our original table tbl0, and the new table tbl1, this means we can perform a 16-bit, i.e. 2-byte, CRC as follows:

```
    CRC(M) = CRC(Az) ^ CRC(B) = tbl1[A] ^ tbl0[B] = tbl1[M >> 8] ^ tbl0[M & 0xFF]
```

Our loop body now looks like the following:


### OPTION 7: 2-byte Tabular ###

```
    uint32_t crc32(const uint16_t* M, uint32_t len_m) {
        uint32_t R = 0;
        for (uint32_t i = 0; i < len_m >> 1; ++i) {
            R ^= M[i];
            R = (R >> 16) ^
                tbl0[uint8_t(R >> 8)] ^
                tbl1[uint8_t(R >> 0)];
        }
        return R;
    }
```

Note that M is now accessed as a series of 16-bit chunks rather than 8-bit ones, and therefore is a uint16_t pointer.

Note also that I shift len_m to the right by 1 bit, i.e. divide by 2, i.e. only len_m/2 total loop iterations, because each iteration processes 2 bytes instead of 1.

Note also that I use >> 0 instead of omitting the shift for symmetry; don't worry, the compiler will throw away this operation and it won't slow anything down.

Note also that I use uint8_t(x), i.e. cast this value to an 8-bit value, instead of x & 0xFF. Either is fine and should produce about equivalent codegen on modern compilers, so I show the alternate approach here so both are familiar.

Note also that tbl0 and tbl1 are accessed in the reverse order from what is expected from our mathematical derivation. This is because x86 is LITTLE-ENDIAN, which means that when you load a value from memory consisting of more than a single byte, you get the bytes in reverse order from how they are stored sequentially in memory:

If in memory you have a message M consisting of bytes:

```
    A B C D E F G H ...
    ^
```

And you have a pointer at the location of the caret, i.e. pointing to A, and you access 8 bits, you get:

```
    R = A
```

As expected.

However if you access 16 bits, you get:

```
    R = BA
```

And if you access 32 bits, you get:

```
    R = DCBA
```

This is why we access tbl0 for R >> 8 (it corresponds to M[i]) and tbl1 for R >> 0 (it corresponds to M[i + 1]).

Option 7 processes 16 bits per iteration, and although it requires more basic operations and 1 more memory access than Option 6, more of them can be performed in parallel, including the latency waiting for the table lookups to arrive, so the improvement to performance is considerable: Option 7 achieves about 1.7 bits per cycle.

The natural question is whether we can further exploit this approach to process even more bytes at once, and, indeed, we can. First, though, a little housekeeping. We COULD use two different tables, tbl0 and tbl1, but we definitely want tbl1 to be contiguous with tbl0, coming directly after it with no empty space in memory, for both simplicity of access and for cache performance. There are ways to achieve this while keeping the separate 'tbl0' and 'tbl1' identifiers, but for simplicity, especially as we're about to add more tables, let's make it one big table:

```
    uint32_t crc32(const uint16_t* M, uint32_t len_m) {
        uint32_t R = 0;
        for (uint32_t i = 0; i < len_m >> 1; ++i) {
            R ^= M[i];
            R = (R >> 16) ^
                tbl[0 * 256 + uint8_t(R >> 8)] ^
                tbl[1 * 256 + uint8_t(R >> 0)];
        }
        return R;
    }
```

Where tbl is our single 2 KB table (512 entries).

So access to tbl0 is unchanged, and we access tbl1 by simply adding 256 to our index in order to move past the 256 entries of tbl0 and into tbl1. As with "R >> 0", we keep the 0 * 256 for symmetry; the compiler won't actually generate any instructions for it.

Expanding this to 32 bits at a time follows the same pattern, except that since we shift out the ENTIRE 32 bits of the previous R in the process, we no longer need a shift R term at all.

### OPTION 8: 4-byte Tabular ###

```
    uint32_t crc32(const uint32_t* M, uint32_t len_m) {
        uint32_t R = 0;
        for (uint32_t i = 0; i < len_m >> 2; ++i) {
            R ^= M[i];
            R = tbl[0*256 + uint8_t(R >> 24)] ^
                tbl[1*256 + uint8_t(R >> 16)] ^
                tbl[2*256 + uint8_t(R >> 8)] ^
                tbl[3*256 + uint8_t(R >> 0)];
        }
        return R;
    }
```

Note that the uint8_t cast on R >> 24 isn't necessary, since R >> 24 can be no larger than 8 bits anyway, but all modern compilers know this and don't generate any extra instructions, so once again, I keep it for symmetry between the 4 lines.

We now shift len_m to the right by 2 bits, i.e. divide by 4, i.e. only len_m/4 total loop iterations, because each iteration processes 4 bytes.

tbl is now a 4 KB table (1024 entries).

Option 8 achieves about 2.7 bits per cycle.


We can keep going! This won't last forever, as our table is getting larger and we're consuming more and more CPU registers to hold all the intermediate data as it comes in, but let's try 8 bytes at a time.

This works just like the previous example, but because we're accessing more data than the size of R, we XOR in just 32 bits of message into R, then separately load in the next 32 bits for use in the table lookup process.


### OPTION 9: 8-byte Tabular ###

```
    uint32_t crc32(const uint32_t* M, uint32_t len_m) {
        uint32_t R = 0;
        while (len_m) {
            R ^= *M++;
            const uint32_t R2 = *M++;
            R = tbl[0 * 256 + uint8_t(R2 >> 24)] ^
                tbl[1 * 256 + uint8_t(R2 >> 16)] ^
                tbl[2 * 256 + uint8_t(R2 >> 8)] ^
                tbl[3 * 256 + uint8_t(R2 >> 0)] ^
                tbl[4 * 256 + uint8_t(R >> 24)] ^
                tbl[5 * 256 + uint8_t(R >> 16)] ^
                tbl[6 * 256 + uint8_t(R >> 8)] ^
                tbl[7 * 256 + uint8_t(R >> 0)];
            len_m -= 8;
        }
        return R;
    }
```

We also slightly change the loop structure, directly advancing the M pointer each time we load another 4 bytes from it, since we now access it twice within each loop iteration.

tbl is now an 8 KB table (2048 entries).

Option 9 achieves about 4.8 bits/cycle.


We can keep going to 16...


### OPTION 10: 16-byte Tabular ###

```
    uint32_t crc32(const uint32_t* M, uint32_t len_m) {
        uint32_t R = 0;
        while (len_m) {
            R ^= *M++;
            const uint32_t R2 = *M++;
            const uint32_t R3 = *M++;
            const uint32_t R4 = *M++;
            R = tbl[ 0 * 256 + uint8_t(R4 >> 24)] ^
                tbl[ 1 * 256 + uint8_t(R4 >> 16)] ^
                tbl[ 2 * 256 + uint8_t(R4 >> 8)] ^
                tbl[ 3 * 256 + uint8_t(R4 >> 0)] ^
                tbl[ 4 * 256 + uint8_t(R3 >> 24)] ^
                tbl[ 5 * 256 + uint8_t(R3 >> 16)] ^
                tbl[ 6 * 256 + uint8_t(R3 >> 8)] ^
                tbl[ 7 * 256 + uint8_t(R3 >> 0)] ^
                tbl[ 8 * 256 + uint8_t(R2 >> 24)] ^
                tbl[ 9 * 256 + uint8_t(R2 >> 16)] ^
                tbl[10 * 256 + uint8_t(R2 >> 8)] ^
                tbl[11 * 256 + uint8_t(R2 >> 0)] ^
                tbl[12 * 256 + uint8_t(R >> 24)] ^
                tbl[13 * 256 + uint8_t(R >> 16)] ^
                tbl[14 * 256 + uint8_t(R >> 8)] ^
                tbl[15 * 256 + uint8_t(R >> 0)];
            len_m -= 16;
        }
        return R;
    }
```

tbl is now a 16 KB table (4096 entries), which is about as large as is wise to go cache-wise (a good rule of thumb is that your L1 workload should not consume more than about HALF of the L1 cache, so a 16 KB table is pushing it, as the message will also consume some.)

Option 10 achieves about 8 bits/cycle.


As expected from the large number of intermediate registers needed and the overflowed L1 cache, pushing it further, to 32 bytes at a time, produces either minor or no improvement to performance, so option 10 (16 bytes at a time) is a reasonable limit to the tabular approach. 8 bits per cycle isn't bad!


We can still do much better. But before we do, one loose end to tie up: creating the table.

Some implementations choose to precompute the table at compile-time and just store it, hardcoded, in a big array in the program. Others choose to initialize it at runtime, either on program startup or on first use. Either way, we need some code to generate the table.

Let's start by considering just the first 256 elements, i.e. 'tbl0', which, recall, stores the following:

```
    tbl0[i] = CRC(i)
```

Each entry in this 256-entry block is the CRC of the 8-bit value i.

Okay, so we can just use our naive CRC implementation to compute 256 CRCs:

```
    for (int i = 0; i < 256; ++i) {
        uint32_t R = i;
        for (int j = 0; j < 8; ++j) {
            R = R & 1 ? (R >> 1) ^ P : R >> 1;
        }
        tbl0[i] = R;
    }
```

Note that we directly initialize R with i. The canonical way would be initializing R with 0, then XORing in our single message byte, i,
but 0 ^ i == i.

We then process 8 bits through the CRC machine, and we're done. Store the result into tbl[i].

For Option 6, 1-byte tabular, this is all you need - the 256 entry table.

For Option 7, 2-byte tabular, recall that we need the following:

```
    tbl1[i] = CRC(i << 8)
```

Each entry in this 256-entry block is the CRC of the 16-bit value consisting of i followed by 8 zero bits.

We COULD continue the same way, picking up R where we left off from tbl0 and churning another 8 message bits (0 bits) through the CRC machine. However, now that we have created tbl0, we can actually do some bootstrapping, and use Option 6's CRC implementation, the 1-byte tabular, to create these elements faster.

Recall that in the 1-byte tabular approach, we process 1 byte of message data in the following way:

```
    R = (R >> 8) ^ tbl0[uint8_t(R ^ M[i])];
```

In this case our message byte to be processed is all 0 bits, so we have:

```
    R = (R >> 8) ^ tbl0[uint8_t(R ^ 0)];
```

Which is just:

```
    R = (R >> 8) ^ tbl0[uint8_t(R)];
```

This operation will churn another 8 zero bits through the CRC machine. If we feed it CRC(i), it will give back CRC(i << 8). So if we give it tbl0[i], it will give back tbl1[i]. Perfect!

```
    for (int i = 0; i < 256; ++i) {
        const uint32_t R = tbl0[i];
        tbl1[i] = (R >> 8) ^ tbl0[uint8_t(R)];
    }
```

However, we can actually repeat this exact process for any remaining table blocks we need to create, because the pattern repeats! Each new table block is simply the PREVIOUS table block's values, shifted left by another 8 zero bits. Each entry of tbl2 is the CRC of i followed by 16 zero bits. Each entry of tbl3 is the CRC of i followed by 24 zero bits. And so on.

Again, the above operation transforms CRC(i) into CRC(i << 8). This is true for i of any size. Therefore, we can generalize the above operation. Instead of taking tbl0 and producing tbl1, it can take table 'n' and produce table 'n+1', for any n.

Putting it all together:

```
void initialize_tables(const int num_tables) {
    int i = 0;

    // initialize tbl0, the first 256 elements of tbl,
    // using naive CRC to compute tbl[i] = CRC(i)
    for (; i < 256; ++i) {
        uint32_t R = i;
        for (int j = 0; j < 8; ++j) {
            R = R & 1 ? (R >> 1) ^ P : R >> 1;
        }
        tbl[i] = R;
    }

    // initialize remaining tables by taking the previous
    // table's entry for i and churning through 8 more zero bits
    for (; i < num_tables * 256; ++i) {
        const uint32_t R = tbl[i - 256];
        tbl[i] = (R >> 8) ^ tbl[uint8_t(R)];
    }
}
```

You can specify 'num_tables' as desired.


  Option           | num_tables       | elements | tbl size
-------------------|------------------|----------|-----------
   1-byte tabular  | num_tables = 1   | 256      | 1  KB
   2-byte tabular  | num_tables = 2   | 512      | 2  KB
   4-byte tabular  | num_tables = 4   | 1024     | 4  KB
   8-byte tabular  | num_tables = 8   | 2048     | 8  KB
  16-byte tabular  | num_tables = 16  | 4096     | 16 KB



Okay, back to making it faster. We're at roughly 8 bits per cycle with the 16-byte tabular approach. Where to from here?

The approach looks pretty optimal. We're using precomputed values to alleviate runtime computation, and consuming about as much L1 as is reasonable. In addition, many operations can occur in parallel, making good use of a modern processor.

However, for better or worse, x86 is a very complex architecture which supports some highly specific operations. We can further accelerate our CRC implementation by making use of some of them.

In particular, the SSE4.2 instruction set extension added an instruction with the mnemonic "crc32". The Intel Software Developer's Manual describes its function as "accumulate CRC32 value". We can take advantage of this instruction to sidestep the tabular approach entirely and further improve performance.

Wait, so why did we waste all that time with the tables if there's a faster way that doesn't use them at all?

Several reasons. We will build on the mathematical basis we developed for the previous approaches. Also, all of the previous approaches are common in the wild, so it's worth being familiar with their principles. Also, embedded, non-x86, and old x86 architectures may not support the complex processor instructions we're about to use to accelerate our CRC, but for those architectures, one of the previous approaches is likely optimal and should be implementable directly or with only minor modification. Also, the previous approaches permit arbitrary tweaks to bit reflection of input and output and an arbitrary polynomial; the upcoming acceleration hardcodes some of these properties, so if you need more flexibility, the previous approaches are superior. And finally, because really understanding the origins and edges of a thing is fun!


The crc32 x86 instruction effectively performs an iteration of our previous approaches' outer loop. In other words, it churns 1 additional message byte through the CRC machine. To use it, you provide the current value of the R register, as well as M[i], and it returns the updated R. In fact, there are multiple variants of the instruction, which can process 1, 2, 4, or 8 additional message bytes at once.

All major compilers expose "intrinsic" functions for the various forms of this instruction, which are C/C++ functions that you can call to request the instruction be generated directly, in the header "nmmintrin.h" (i.e. you must #include <nmmintrin.h> to use them). Their declarations look like this:

```
    uint32_t _mm_crc32_u8 (uint32_t R, uint8_t  M);
    uint32_t _mm_crc32_u16(uint32_t R, uint16_t M);
    uint32_t _mm_crc32_u32(uint32_t R, uint32_t M);
    uint64_t _mm_crc32_u64(uint64_t R, uint64_t M);
```

Each instruction takes the current state of the R register and either 1, 2, 4, or 8 additional message bytes to churn through the CRC machine. It does so, then returns the updated state of the R register.

Don't be confused by the fact that the last version, the one that processes 8 bytes (64 bits) of additional message data at a time, actually takes and returns a 64-bit value for R; this is a quirk of the instruction encoding and is NOT actually used. Internally, this version treats R exactly the same as all the other versions - as a 32-bit value.

Note that unlike our previous approaches, which could be used with any polynomial of your choice, the crc32 instruction hardcodes the following polynomial with n = 33, i.e. w = 32, commonly referred to as the "CRC32-C" poly:

```
    P = 0x11EDC6F41 = 0b100011110110111000110111101000001
```

Recall that the width of a poly is dependent on its highest (leftmost) set bit, since that's where we stop counting the size. Therefore the highest bit is always set. This is why we can safely keep just the low (rightmost) n - 1 bits, i.e. w, when performing our previous approaches' algorithms - the leftmost bit is implied.

Therefore the uint32_t of this P that we'd actually store for our previous approaches omits the high (leftmost) bit:

```
    P = 0x1EDC6F41 = 0b00011110110111000110111101000001
```

Recall that as part of optimizing our implementation, however, we bit-reflected our R. Therefore to use this P, we actually require one more step: bit-reflect (reverse the bit order) of this 32-bit poly. This produces:

```
    P = 0x82F63B78 = 0b10000010111101100011101101111000
```

With this as our final P value for our previous approaches, they will match exactly the output from our upcoming hardware-accelerated crc32 implementation.

We can start with _mm_crc32_u8, which processes 1 byte of additional message data. It is therefore an exact drop-in replacement for the contents of our "outer loop" from our 1-byte tabular approach.

I've reprinted the 1-byte tabular approach here for easy reference:

```
    uint32_t crc32(const uint8_t* M, uint32_t len_m) {
        uint32_t R = 0;
        for (uint32_t i = 0; i < len_m; ++i) {
            R = (R >> 8) ^ tbl[(R ^ M[i]) & 0xFF];
        }
        return R;
    }
```

And replacing the inner loop body with the 1-byte crc32 intrinsic, we get:

### OPTION 11: 1-byte Hardware-accelerated ###

```
    uint32_t crc32(const uint8_t* M, uint32_t len_m) {
        uint32_t R = 0;
        for (uint32_t i = 0; i < len_m; ++i) {
            R = _mm_crc32_u8(R, M[i]);
        }
        return R;
    }
```

Exactly the same as our 1-byte tabular approach, but with no table! We complete an outer loop iteration, processing 1 byte, using the _mm_crc32_u8 intrinsic.

This approach achieves about 2.66 bits/cycle. This is actually its theoretical peak, because at the time of writing, the crc32 instruction takes 3 cycles to execute on modern x86 architectures, REGARDLESS of which variant is used. In this option it therefore processes 1 byte (8 bits) every 3 cycles, which works out to 2.666... bits/cycle. This can be verified experimentally.

Note that this is much faster than the 1-byte tabular approach (about 1.1 bits/cycle), but not as fast as the best tabular approach, which churned 16 bytes at a time and achieved about 8 bits/cycle.

However, we're just getting started with the crc32 instruction - recall that it's a family of instructions capable of processing 1, 2, 4, or even 8 bytes at a time.

Let's just jump straight to the maximum, then, and do 8 bytes at a time. Because they all take the same number of cycles to execute, this will be our fastest option.

### OPTION 12: 8-byte Hardware-accelerated ###

```
    uint32_t crc32(const uint64_t* M, uint32_t len_m) {
        uint64_t R = 0;
        for (uint32_t i = 0; i < len_m >> 3; ++i) {
            R = _mm_crc32_u64(R, M[i]);
        }
        return (uint32_t)R;
    }
```

Note that we now shift len_m right by 3 bits, i.e. divide it by 8, before beginning: we only need to perform 1/8 as many loop iterations, since we are processing 8 bytes of message per iteration instead of just 1.

Note also that we declare R as a 64-bit, instead of a 32-bit, value this time, and only truncate it down to 32 bits at the end, when returning it out of the function. This is because of the aforementioned quirk in the x86 instruction set encoding of the crc32 instruction. This version takes and returns a 64-bit value for R, even though internally it only cares about the low 32 bits, just like the rest of the instructions in the crc32 family.

Compilers, however, may not KNOW that the instruction internally doesn't care about the high 32 bits. All they see is an instruction taking a 64-bit value and returning one. If we were to use a 32-bit value, the compiler might waste cycles inserting instructions that zero-extend our 32-bit incoming value to 64 bits prior to passing it to the crc32 instruction.

At the time of writing, some, but not all, major compilers are smart enough to know the internals of the crc32 instruction are actually 32-bit and therefore generate good code even if you give it a 32-bit value for R. But not all! (MSVC gets it wrong). So we use a 64-bit value here to make sure we get optimal codegen on all compilers.

This version is pretty awesome. We should be able to predict its performance based on what we know about the crc32 instruction: takes 3 cycles and, in this option, processes 8 bytes (64 bits) at a time. Therefore, we have 64/3 = 21.33 bits per cycle. And sure enough, this is just about exactly what we find experimentally: around 21.3 bits per cycle.

So it's super fast, way faster than our next-best approach (16-byte tabular, 8 bits per cycle). It's also extremely compact and easy to follow, and it requires no table. How could we possibly do better than a hardware-accelerated crc32 instruction processing 64 bits at a time and in only 3 cycles?

Well, we kind of can't, in the sense that our next approach will still involve the crc32 instruction. But there IS still room for improvement.

The insight that will permit improvement is that when I say the crc32 instruction "takes 3 cycles", I'm not telling the full story. The crc32 instruction, in modern Intel x86 processors, has a LATENCY of 3 cycles, and a THROUGHPUT of 1 per cycle.

Modern processors are very complicated, and it's no longer possible to state the performance of an instruction as a single number of cycles. Latency and throughput are two different aspects of "how long" an instruction takes, and both can be important.

Modern processors have out-of-order (OoO) execution and instruction-level parallelism (ILP). Note that this is separate from the idea that a single chip can have multiple processors, multiple cores, multiple threads of execution, etc. Within a SINGLE thread of execution running on a SINGLE core, a stream of instructions can execute in a different order than the order they are written in, and also, multiple instructions can execute simultaneously. And because many instructions take longer than 1 cycle to execute, this means that at any given snapshot of time, a single thread of execution in a modern processor can have multiple instructions in progress, at various stages of completion, all at the same time.

With this in mind, it's hard to reason about how a single instruction affects the course of execution, particular as when it executes will depend on complex interactions with neighboring instructions.

To simplify things a bit, let's assume an instruction stream consisting of an infinite number of just the one kind of instruction we're trying to measure. This will simplify analysis but still give us useful results.

Let's invent a pseudo-instruction called 'instr' to use in examples. This instruction will have two operands: a destination register and a source register.

There are two very different kinds of infinite streams we could have here:

```
    Type 1: Latency-bound

    ...
    instr a, a
    instr a, a
    instr a, a
    ...
```

So this is a long stream consisting of a large number of 'instr' instructions, each taking register 'a' as an input AND an output. In other words, each instruction is DEPENDENT on the preceding one, because its input value will be set by the output of the preceding instruction.

Therefore, no parallel execution is possible. The processor cannot begin executing an instruction until its inputs are available, and in this case, the input to each instruction is only available once the preceding instruction has completely finished execution.

I chose this stream to examine first because it exactly mirrors our crc32 use case: given a large chunk of data, we are going to perform a long string of crc32 instructions, each of which is DEPENDENT on two inputs: R, and the new message data. But the output gets stored back into R. Therefore, just like with this hypothetical 'instr' example, the processor cannot begin executing a crc32 until the preceding one has finished, because the preceding one will determine the R value to be passed to the next one.

Such a stream of instructions, each dependent on the previous, is called a LATENCY-BOUND stream, or a DEPENDENCY CHAIN. Analyzing such a chain is a more tractable problem. The amount of cycles added to total execution time by inserting a single additional instruction is called the LATENCY of the instruction.

If we were to number the instructions in our example stream:

```
          ...
    100   instr a, a
    101   instr a, a
    102   instr a, a
          ...
```

In this example, if 'instr' has a 3 cycle latency, like crc32 does, then instruction #101 adds 3 cycles to total execution time. To put this another way, instruction #102 would begin executing exactly 3 cycles after #100 finished executing. That's "how long" instruction #101 would take to execute in between.

There is, however, another possible way to structure a stream:

```
    Type 2: Throughput-bound

    ...
    instr b, a
    instr b, a
    instr b, a
    ...
```

Here we have a long stream of instructions, each taking register 'a' as input but 'b' as output. Each instruction is INDEPENDENT! As soon as the stream begins, all instructions in the stream are eligible for execution. Their input, 'a', is available from the start.

If a processor had infinite instruction-level parallelism (ILP), it could therefore, technically, execute the whole stream simultaneously.

In practice, of course, there are limits to how much parallelism even the highest-end modern processors can provide, and this varies per instruction. For some instructions, the processor is able to support multiple instances of the instruction "in-flight" simultaneously - as long as the instances are independent of each other, of course.

Such a stream of INDEPENDENT instructions is called a THROUGHPUT-BOUND stream.

The average number of single instructions in such a stream processed per unit time, i.e. the number of instructions in the stream divided by the number of cycles taken, is called the THROUGHPUT of the instruction.

The throughput therefore has units of instructions per cycle, in contrast to latency, which is cycles per instruction. To bring it into parity, throughput is sometimes reported as RECIPROCAL THROUGHPUT instead, which is simply the inverse of throughput:

```
    1 / throughput = reciprocal throughput
```

Reciprocal throughput therefore has units of cycles per instruction, just like latency.

If a processor has no parallel capability for a particular instruction, latency will equal reciprocal throughput. In our 'instr' example, let's imagine that 'instr' has no parallel capability, and has a 3-cycle latency and a 3-cycle reciprocal throughput.

If we were to plot the processor's internal state over time executing our Type 2 instruction stream, using the letters A, B, and C to represent the 3 stages (cycles) of instruction execution, it would look like this:

```
    Pipe 1  | A | B | C | A | B | C | A | B | C |
            |
            |           x           x           x
    -- Time -->
```

The 'x' characters along the time axis represent instruction completions. You can see that in this example, the first instruction completes 3 cycles after starting up. We then continue to complete one instr every 3 cycles. There's no parallelism, and each takes 3 cycles. Latency and reciprocal throughput are both equal to 3 cycles.


But now let's imagine that the processor has three internal pipes that can handle this instruction. This means the processor can support up to 3 parallel (IF they are independent!) instrs at once. Let's also assume the processor can begin ONE new instruction of this kind per cycle. Now the plot looks like this:

```
    Pipe 1  | A | B | C | A | B | C | A | B | C |
    Pipe 2  |   | A | B | C | A | B | C | A | B | C |
    Pipe 3  |   |   | A | B | C | A | B | C | A | B | C |
            |
            |           x   x   x   x   x   x   x   x   x
    -- Time -->
```

The first instruction still completes 3 cycles after starting up - that's the latency talking. But because we have three execution pipes, we enqueue three parallel executions of the instruction, each offset by 1 cycle. Each instruction takes 3 cycles (its LATENCY) to complete, but three of them are running at once, each offset by 1 cycle, so we can sustain a THROUGHPUT that is 3x higher than our LATENCY would indicate. As you can see from the plot, after our first instruction completes (after cycle 3), we complete a new instruction EVERY CYCLE from then on.

Therefore, in this example, the reciprocal throughput of instr is 1 per cycle, even though the latency is 3 cycles.

As it turns out, this is exactly the situation the crc32 instruction is in on modern Intel x86 processors. We can have up to three crc32 instructions in flight at once, in parallel, if they're independent. Latency 3, throughput 1.

So we're not running crc32 as fast as we could be! Right now we're only kicking a new crc32 every 3 cycles, because our stream of crc32 instructions is currently LATENCY-BOUND (Type 1). Each new crc32 is dependent on the previous one.

If we could find some way to convert this into a throughput-bound stream (Type 2), we could kick a new crc32 every cycle and achieve a three-fold performance increase!

But this is not easy. CRC is inherently a serial process: we're iterating over the message, maintaining the state of this internal register R, and then walking along the message. Each chunk mutates R, so the next chunk is inherently dependent on the previous.

Okay, so what if we take an approach similar to our multi-byte tabular methods, and perform some independent CRCs of neighboring chunks of data, then combine the results mathematically?

This will be more complicated than last time. The goal is to derive an expression equivalent to CRC(M) consisting of three independent CRCs.

To that end, let's break the message M into 3 equal chunks, A, B, and C. And let's go ahead and make that an actual term I will use going forward: a CHUNK is one of these three equal-sized pieces of the message, M:

```
    M = ABC
    CRC(M) = CRC(ABC) = CRC(A00 ^ B0 ^ C) = CRC(A00) ^ CRC(B0) ^ CRC(C)
```

But we have a problem: the first term, A00, is just as long as M, and will take just as long to CRC! We need a way to account for the 00 part, i.e. the shifting to the left of the A portion, in a way that doesn't still require CRCing the full length of the message (and similarly for the smaller shift of the B portion).

Let's derive an identity that will help accomplish this. For any portion Z of a message, and where x is the number of bits we want to shift left by:

```
    CRC(Z << x)
    = (Z << x << w) mod P
    = (Z << w << x) mod P
    = ((Z << w) * (1 << x)) mod P
    = ((Z << w) * (1 << x - w << w)) mod P
    = ((Z << w) mod P * (1 << x - w << w) mod P) mod P
    = (CRC(Z) * CRC(1 << (x-w))) mod P
```

At first, this doesn't seem any better. We have to CRC the portion (Z), but also the value (1 << (x-w)), which is just 1 followed by x-w zeros, but that is a block of data of size x-w bits, i.e. almost as big as the amount to shift.

However, note that this value is INDEPENDENT of the message data! It is dependent only on x, the number of bits shifted. It is therefore possible to precompute the value of CRC(1 << (x-w)) and store it in our program, for a given shift amount. If we need to support multiple shift amounts, we can precompute all the ones we want, and store them in a LUT (look-up table), like we did with the previous tabular methods (except those were storing the CRC of various bytes, whereas here we'd be storing the CRC of 1 followed by various amounts of zeros).

Other sources suggest using a matrix-matrix method to shift over the excess zeros, but this is NOT free: at best, it can achieve a logarithmic, or O(log(n)), additional penalty over message length, whereas the above derived result has no additional penalty (O(1)) for zeros. Overall runtime remains linear (O(n)) in the length of the message.

Recall that we need to break our message into 3 parts so that we can have 3 independent crc32 instructions in-flight concurrently. Therefore we apply the identity we derived above to a 3-way split of our message M:

```
    CRC(M)
    = CRC(ABC)
    = CRC(A << 2x) ^ CRC(B << x) ^ CRC(C)
    = ((CRC(A) * CRC(1 << (2x-w))) mod P) ^ ((CRC(B) * CRC(1 << (x-w))) mod P) ^ CRC(C)
```

Okay, this looks promising, but one problem remains: there are some loose 'mod P's in there. Or to think about it intuitively: we're multiplying two CRCs together, but the result of such a multiplication (even if it's a carryless multiplication, as in this case) can be as long as 2w bits, i.e. it's no longer in the ring modulo P. We have to do one last mod P afterward to bring it back into the ring, and this truth is captured mathematically in the presence of the two mod Ps.

If we are to use the hardware x86 instruction, which does a whole CRC, we don't have any efficient way of JUST running a 'mod P'. We have to find a way to wrap this expression so that it only uses CRCs, i.e. a shift left of w followed by a mod P, without any raw 'mod P's occurring directly.

The simplest way to get rid of a mod P is to make sure it takes place inside a CRC:

```
      CRC(M mod P)
    = ((M mod P) << w) mod P
    = (M mod P * (1 << w)) mod P
    = (M * (1 << w)) mod P
    = (M << w) mod P
    = CRC(M)
```

So we have that for any message M:

```
    CRC(M mod P) = CRC(M)
```

In other words, it is valid to freely add or remove 'mod P' operations from the argument of CRC, and in fact, this can be generalized to any term that's XOR'd in as part of a CRC:

```
      CRC((A mod P) ^ B)
    = ((((A mod P) ^ B) mod P) << w) mod P
    = (((A mod P) ^ B) mod P * (1 << w)) mod P
    = ((A ^ B) mod P * (1 << w)) mod P
    = ((A ^ B) * (1 << w)) mod P
    = ((A ^ B) << w) mod P
    = CRC(A ^ B)
```

So we have that for any two XOR'd terms of a CRC, A and B:

```
    CRC((A mod P) ^ B) = CRC(A ^ B)
```

It is valid to freely add or remove 'mod P' operations from any XOR'd term of a CRC.

Therefore, in order to get rid of our stray mod Ps, it seems we need to wrap the whole thing in another call to CRC(). But we can't JUST do that, because then we'd have CRC(CRC(M)), which is not the desired result.

There is a way, however. Again looking at it intuitively, we can just defer processing the last 64-bit chunk of C and fold in the results from A and B (via an XOR) first, THEN apply our final CRC, which will wrap the whole thing as desired.

To demonstrate this mathematically (and show that it's valid), let C be split into D and E, where D is all BUT the last 64 bits of C, and E is the last 64 bits of C:

```
    CRC(M)
    = CRC(ABDE)
    = CRC((ABD << 64) ^ E)
    = CRC((A << 2x) ^ (B << x) ^ (D << 64) ^ E)
    = CRC(CRC(A << (2x-32)) ^ CRC(B << (x-32)) ^ CRC(D << 32) ^ E)
    = CRC(CRC(D << 32) ^ CRC(A << (2x-32)) ^ CRC(B << (x-32)) ^ E)
    = CRC(CRC(D << 32) ^ (CRC(A) * CRC(1 << 2x-64)) ^ (CRC(B) * CRC(1 << x-64)) ^ E)
```

This is our final, "golden" result we need. I'll repeat it.

- - - -
```
    CRC(M) = CRC(CRC(D << 32) ^ (CRC(A) * CRC(1 << 2x-64)) ^ (CRC(B) * CRC(1 << x-64)) ^ E)
```
- - - -


It tells us that to calculate CRC(M), for any M, we can break it into 3 equal CHUNKS, A, B, and C. We then further break C into two parts, D (all but the last 64 bits) and E (the last 64 bits), and then evaluate the above "golden" expression.

Remember, the point of all this math was to find an expression that lets us run 3 independent CRCs in parallel, each of which is not longer than 1/3 of the length of M, so that total runtime will be 1/3 of what it was. And this expression achieves the goal! We can separately run CRC(A), CRC(B), and CRC(D << 32), look up CRC(1 << 2x-64) and CRC(1 << x-64) in a table we precompute and store, and then one final outer CRC completes the workload.


I don't like that D << 32 though, so let's use a property of the x86 CRC instruction family to get rid of it. Recall that the instruction actually takes two arguments (the starting value of the R register, and the message), even though we've been treating it as only taking 1 (the message). This allows chaining invocations of the instruction such that you can keep accumulating a longer CRC. If you're walking along a message 64 bits at a time, accumulating a CRC, you pass the instruction the current CRC as well as the next 64 bits to process. So for a message M consisting of X (all but the last 64 bits) and Y (the last 64 bits), we have:

```
    CRC(M)
    = CRC(XY)
    = CRC(CRC(X), Y)
```

But we can also derive a mathematical equivalency here:

```
    CRC(M)
    = CRC(XY)
    = CRC((X << 64) ^ Y)
    = CRC(CRC(X << 32) ^ Y)
```

Therefore we have:

```
    CRC(CRC(X << 32) ^ Y) = CRC(CRC(X), Y)
```

If the length of Y is 64 bits.

This is precisely the situation we have in our golden result with the D term:

```
    CRC(M)
    = CRC(CRC(D << 32) ^ (CRC(A) * CRC(1 << 2x-64)) ^ (CRC(B) * CRC(1 << x-64)) ^ E)
    = CRC(CRC(D), (CRC(A) * CRC(1 << 2x-64)) ^ (CRC(B) * CRC(1 << x-64)) ^ E)
```

And so this is the version of the golden result we will use for implementation. I'll repeat it.

- - - -
```
    CRC(M) = CRC(CRC(D), (CRC(A) * CRC(1 << 2x-64)) ^ (CRC(B) * CRC(1 << x-64)) ^ E)
```
- - - -

Just a few more implementation details to address.

First, that precomputed table. We need to generate it, and see how few entries we can get away with.

Well, we will be using the variant of the x86 crc32 instruction that processes 64 bits at a time. When we split up our message into 3 equal chunks (each of length x), it is reasonable for now to require each part to be a multiple of 64 bits (don't worry, we will loosen this restriction, and several others, later).

As we will be working exclusively in such multiples of 64 bits, I will go ahead and define BLOCK to mean 64 bits of message data processed at once by a single execution of the 64-bit version of the crc32 hardware instruction.

I will further define a variable 'n' to mean the number of such blocks in a chunk:

```
    n = x / 64
```

n, then, is the BLOCK COUNT, and I will use this terminology going forward.


So: we will only require an entry in the table for multiples of 64, and we don't need the nonsensical case x=0, so our table need only contain: x=64, x=128, x=196, and so on. Therefore we will index into the table with an index n - 1 == x / 64 - 1 == (x - 64) / 64:


  n-1  |  x
-------|------
   0   |  64
   1   |  128
   2   |  196
  ...


But how high do we go? In order to support messages of arbitrary length, it seems we need an infinitely long table!

Fortunately, there is a way. Instead of spreading our 3 equal chunks out to cover the whole message, we limit the maximum size of the 3 equal chunks to something reasonable. If the message is longer than that, after completing the CRC of that part, we move on and repeat the same parallel process on the NEXT 3 equal chunks of the message. And so on, until we reach the end of the message.

Therefore, it's actually up to US how large to make the table.

Of course, if all messages were a fixed size, we wouldn't need a table, just a couple constants. But if we want to support arbitrary messages, we need to have table entries ranging from 1 block up to whatever we decide is our maximum chunk size.

Does that mean we could set our maximum chunk size to 1 block (i.e. x=64) and then just have a single pair of constants?

Technically yes, but the problem is all the overhead we'd incur computing our golden result from earlier. The whole point of doing this is so a modern Intel CPU can spend every cycle with 3 independent CRCs in the pipeline. Every time we stop that pipeline to do our multiplications, we waste cycles.

If we do this after EVERY triplet of CRC instructions, as we would if x = 64, the overhead would vastly outweigh actual CRC computation time, and it would be slow!

The longer our chunks (i.e. the larger our n), the more time we spend just doing a long stream of CRCs, compared to the fixed cost of the overhead at the end. Eventually, n gets large enough that this overhead is insignificant, and that's a good place to end our table.

I've limited mine to n = 256. By this point the overhead is a negligible part of total runtime. Even 128 is fine; further improvement in performance at 256 is only noticeable for fairly large M.

That's 2*256=512 entries, 32 bits each. Not bad.

To construct the table, we'll use a technique similar to how we generated the tables for our Tabular method. In this case, we start with the value 1 in R. For each new entry, we churn 64 zero bits through the CRC machine, then store out the new table entry and continue churning. We repeat until we have 512 entries:

```
    void initialize_golden_lut() {
        uint32_t R = 1;
        for (uint32_t i = 0; i < 512; ++i) {
            tbl[i] = R;
            for (uint32_t j = 0; j < 64; j++) {
                R = R & 1 ? (R >> 1) ^ P : R >> 1;
            }
        }
    }
```

Next we must address the multiplies that appear in the golden result. Remember, those aren't normal integer multiplies. Those are CLMULs (carry-less multiplies)!

We need a way to implement those. Recall the simple implementation of carryless multiplication:

"So the result of polynomial multiplication mod 2 is just the XOR'ing together of some shifted-over copies of the top number: one for each set bit position in the bottom number."

We can implement this directly:

```
    uint64_t clmul(const uint32_t a, const uint32_t b) {
        uint64_t ret = 0;
        for (uint32_t i = 0; i < 32; ++i) {
            if ((a >> i) & 1) {           // if the i'th bit of a is set...
                ret ^= (uint64_t)b << i;  // ...xor in b, shifted left by i
            }
        }
        return ret;
    }
```

MSVC compiles this very poorly; other compilers do okay. MSVC makes two mistakes: it doesn't fully unroll the loop, and it also generates jumps instead of conditional moves. There's no way to fix the first mistake, as MSVC does not allow loop unroll hints like other compilers do (other than vector independence hinting, which is not useful here). However we can trick it into fixing the second mistake:

```
    uint64_t clmul(const uint32_t a, const uint32_t b) {
        uint64_t ret = 0;
        for (uint32_t i = 0; i < 32; ++i) {
            dest ^= (a >> i) & 1 ? ((uint64_t)b << i) : 0;
        }
        return ret;
    }
```

Because of the first mistake (no loop unrolling), this will still be about 3 times slower than other compilers.

If you absolutely need this to be faster under MSVC, you can manually unroll this, writing out the inner loop statement 32 times. Rather ugly, but it works. If you're in the mood for over-engineering, you can also use preprocessor macros or templates to automate this "manual unroll".

Beyond that, there is no way (that I know of!) to optimize this further unless you know in advance either 'a' or 'b' and can do some precomputation accordingly.

And it's kinda slow.

However, modern x86 processors from both Intel and AMD actually have a HARDWARE clmul instruction! Nonetheless, in addition to being good practice, the above implementation may prove useful if you don't have access to this instruction. The "golden" strategy is still good if you're in that situation - just make a bigger table since the overhead will be higher.

So how do we access fast hardware CLMUL? Not very easily. Although in this application we only need to multiply two 32-bit values (producing a 64-bit product), the instruction is designed to be able to multiply two 64-bit input operands, outputting a 128-bit product.

But general-purpose registers (GPRs) on x86 are only 64 bits wide! The instruction set designers had two options: have the instruction place its return value into TWO registers, or make this a vector instruction, operating on XMM registers, which are 128 bits wide.

They chose the latter: the clmul instruction takes two XMM registers and an immediate value as inputs. The immediate value tells the instruction which 64-bit halves of the input vector registers you want to multiply together. It places the 128-bit product in another vector register as output.

Therefore, to USE this instruction in our application, we have to move our two 32-bit multiplicands into vector registers, then perform the clmul, then move the result back into a GPR. I know it sounds like a lot of work, but it's still MUCH faster (~10x) than the manual implementation above.

All modern compilers provide intrinsics that allow us to perform these operations.

A further optimization we can perform here is that we will always need to load the pair of table constants for x-64 and 2x-64 simultaneously, so we can arrange our table to group those together into a single 64-bit value and thereby load both constants with a single 64-bit load:

    void print_golden_lut() {
        printf("const uint64_t lut[] = {\n");
        for (uint32_t i = 0; i < 256; ++i) {
            printf("0x%08x%08x,%c", tbl[i], tbl[(i << 1) + 1], (i & 3) == 3 ? '\n' : ' ');
        }
        printf("};\n");
    }

Note that we print our table as 256 packed 64-bit values, each consisting of indices i and 2i + 1, i.e. x - 64 and 2x - 64, as desired.


Next we must wrestle with the compiler to get it to produce efficient codegen for the fairly complex operation I want to perform. I'd like a big waterfall of CRC32 instructions, with absolutely nothing else in between, referencing the message data directly in memory, so we get the maximally efficient pipelining that's the whole point of this process. To make this work, I want precisely the following codegen. Note that I'm ignoring the special case CLMUL stuff that would be at the end of this process, for now:

```
    ...
    CASE_5:
     crc32       crcA, [end_of_A_chunk-40]
     crc32       crcB, [end_of_B_chunk-40]
     crc32       crcC, [end_of_C_chunk-40]

    CASE_4:
     crc32       crcA, [end_of_A_chunk-32]
     crc32       crcB, [end_of_B_chunk-32]
     crc32       crcC, [end_of_C_chunk-32]

    CASE_3:
     crc32       crcA, [end_of_A_chunk-24]
     crc32       crcB, [end_of_B_chunk-24]
     crc32       crcC, [end_of_C_chunk-24]

    CASE_2:
     crc32       crcA, [end_of_A_chunk-16]
     crc32       crcB, [end_of_B_chunk-16]
     crc32       crcC, [end_of_C_chunk-16]

    CASE_1:
     crc32       crcA, [end_of_A_chunk-8]
     crc32       crcB, [end_of_B_chunk-8]
     crc32       crcC, [end_of_C_chunk-8]
```

Triplets of interleaved INDEPENDENT CRC instructions, accumulating 3 different CRCs into 3 different regs. Arranging it this way, counting relative to the END of the data chunks, allows the offsets to be independent of the size of the chunks, since the last case will always be handling the last 8 bytes of the chunk, and so on. In other words, this waterfall structure allows us to JUMP directly to the location appropriate for the current chunk size.

For example, if our chunks were only 8 bytes long, i.e. a chunk size of n = 1, we could jump directly to the CASE_1 label. One triplet of crc32s would be performed, and we'd be done. If our chunks were 32 bytes long, i.e. a chunk size of n = 4, we could jump to the CASE_4 label and start executing there. We'd get 4 triplets, in the correct order, completing the chunk for each of our 3 chunks of data (A, B, and C, again ignoring the special-casing around the end of C for now).

To figure out where to jump to, we have two decent options.

Option 1: n * bytes_per_triplet
We can figure out how many bytes of instructions each of the above triplets of crc32s are; it should be consistent. Then, when we reach the top of the waterfall, we simply jump forward by n * bytes_per_triplet, and start executing from there.

But there's a complication. The sizes of the triplets are NOT consistent, sadly. Small constant offsets (less than 256, i.e. that fit in an 8 bit constant) result in a smaller instruction encoding than large constant offsets (greater than or equal to 256). So the bottom few triplets are actually smaller than preceding ones. We could manually generate the same size encoding for all instructions, bypassing both the compiler and even the assembler and instead writing our own instruction bytes. This is my preferred solution for efficiency and compactness, but it would be better to find a solution that allows us to express this code in a high-level language and achieve optimal performance (possibly sacrificing a little bit of code size or some other quantity).

A possible workaround is to change our offset math a bit so there's always a constant offset of at least 256. This is quite doable even in high-level code, but sadly, there's NO way to explicitly express this direct-jump structure in a high-level language. Compilers could definitely figure out to generate it from a suitable high-level implementation, but sadly, NO current compilers will take advantage of the same-size triplets to do so. Therefore we must fall back to option 2:

Option 2: jump table

Instead of jumping directly to the instruction at n * bytes_per_triplet past the start of the waterfall, we use 'n' to index into yet another table, called a jump table, which contains the offsets of the cases (the triplets). We jump forward by the amount we look up in the table.

Obviously, this adds an extra table with 256 entries (one for each possible jump target). If we were handwriting it we could pack it into 2 bytes per entry, possibly even 1 byte with scaling. But alas, compilers are not very smart, and they will produce a 4-byte-per-entry jump table.

While an extra 4*256 = 1024 bytes is not great, runtime performance should be roughly unaffected: instead of calculating the offset and jumping, we look up the offset (from a small table which should reside in L1) and then jump.

This is not the only optimization or size reduction we're leaving on the table in this implementation discussion; I'll list all the ones I can think of later.

The good news is that compilers will generate jump tables from a fallthrough switch statement, which looks like this:

```
    switch (n) {
        case 4:
        // CRCs for triplet 4
        case 3:
        // CRCs for triplet 3
        case 2:
        // CRCs for triplet 2
        case 1:
        // CRCs for triplet 1
    }
```

Those case statements are just labels. When the switch is encountered, the compiler will generate code to jump to the appropriate label matching the current value of 'n'. The case statements are otherwise ignored, so after jumping in and beginning execution, control flow will continue executing straight down through all subsequent cases until the end of the switch is reached, which is exactly what we need in our case.

Okay, we're ready for an (almost) complete implementation in C++:

```
    uint32_t crc32(const uint8_t* M, uint32_t bytes) {
        uint64_t pA = (uint64_t)M;
        uint64_t crcA = 0;

        while (bytes) {
            const uint32_t n = bytes < 256 * 24 ? bytes / 24 : 256;
            pA += 8 * n;
            uint64_t pB = pA + 8 * n;
            uint64_t pC = pB + 8 * n;
            uint64_t crcB = 0, crcC = 0;
            switch (n) {
                case 256:
                    crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA - 8*256));
                    crcB = _mm_crc32_u64(crcB, *(uint64_t*)(pB - 8*256));
                    crcC = _mm_crc32_u64(crcC, *(uint64_t*)(pC - 8*256));
                case 255:
                    crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA - 8*255));
                    crcB = _mm_crc32_u64(crcB, *(uint64_t*)(pB - 8*255));
                    crcC = _mm_crc32_u64(crcC, *(uint64_t*)(pC - 8*255));

                ...

                case 3:
                    crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA - 8*3));
                    crcB = _mm_crc32_u64(crcB, *(uint64_t*)(pB - 8*3));
                    crcC = _mm_crc32_u64(crcC, *(uint64_t*)(pC - 8*3));
                case 2:
                    crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA - 8*2));
                    crcB = _mm_crc32_u64(crcB, *(uint64_t*)(pB - 8*2));
                    crcC = _mm_crc32_u64(crcC, *(uint64_t*)(pC - 8*2));
            }

            crcA = _mm_crc32_u64_t(crcA, *(uint64_t*)(pA - 8));
            crcB = _mm_crc32_u64_t(crcB, *(uint64_t*)(pB - 8));
            __m128i vK = _mm_cvtepu32_epi64(_mm_loadu_si128((__m128i*)(&lut[n - 1])));
            __m128i vA = _mm_clmulepi64_si128(_mm_cvtsi64_si128(crcA), vK, 0);
            __m128i vB = _mm_clmulepi64_si128(_mm_cvtsi64_si128(crcB), vK, 16);
            crcA = _mm_crc32_u64(crcC, _mm_cvtsi128_si64(_mm_xor_si128(vA, vB)) ^ *(uint64_t*)(pC - 8));

            bytes -= 24 * n;

            pA = pC;
        }

        return crcA;
    }
```

The vector stuff in particular can be very tricky, so let's go over it one line at a time.

```
    uint64_t pA = (uint64_t)M;
```

pA is a 64-bit integer pointing to our A block of data. It will start right at the beginning of M.

```
    uint64_t crcA = 0;
```

Initialize our A-chunk CRC to 0. This one is outside the while loop, because it will also serve to accumulate the overall CRC value if we have to do more than one round of processing (which we will if the message is longer than our self-imposed limit of 256 iterations).

```
    while (bytes) {
```

While there is data remaining to process...

```
    const uint32_t n = bytes < 256 * 24 ? bytes / 24 : 256;
```

Recall that this is a ternary expression which is equivalent to:

```
    uint32_t n;
    if (bytes < 256 * 24) {
        n = bytes / 24;
    }
    else {
        n = 256;
    }
```

We are calculating n, the number of blocks in each chunk. We are splitting the bytes into 3 equal parts (A, B, and C) and each block is 8 bytes, so in total we must divide bytes by 8*3=24 to get n.

However, if that value would be greater than 256, we will not process the whole message, and instead limit ourselves to n = 256, because that's how big our LUT (look-up table) is - not to mention our jump table. So in that case we just set n = 256.

```
    pA += 8 * n;
    uint64_t pB = pA + 8 * n;
    uint64_t pC = pB + 8 * n;
```

We move our A pointer to the END of its chunk, since, recall, our switch statement will process data relative to the end of the chunks, not the beginning.
We also create similar B and C pointers to the end of the B and C chunks, respectively.

```
    uint64_t crcB = 0, crcC = 0;
```

Initialize our B-chunk and C-chunk CRCs to 0. These are inside the while loop, because they only exist during parallel processing. After we've done our iterations, they will fold their results into crcA, and crcA alone will accumulate the value and carry it out of the while loop (or back through for another round of processing, if we have more than our self-imposed limit of 256*3 blocks worth of message to process).

```
    switch (n) {
        ...
    }
```

Here's our switch statement. We provide case labels for 256, 255, 254, ..., all the way down to 4, 3, 2. We now LEAVE OUT our '1' case as the special case that we will handle separately, after the switch.

The Clang and MSVC compilers correctly turn this into a jump table. Gcc fails to and produces some pretty ugly jump trampolines instead. The performance is not severely impacted but code size grows unnecessarily.

Inside each case, we process exactly one triplet (advancing each of 3 chunks by 8 bytes), then fall through into the next case.

Note that this is a very long, redundant stream of code. It would be nice if we could just write a loop or something to shorten the code yet produce the same assembly. However, any attempt to do so totally breaks any modern compiler, even if you attempt to force it to unroll the loop. They are simply unable to unroll it properly.

There is a way to shorten it using preprocessor macros, so that the compiler sees exactly the code shown above, but it appears shorter to us when editing the code. I've skipped over this and a few other details for now, but they'll be in the final implementation later.

```
    crcA = _mm_crc32_u64_t(crcA, *(uint64_t*)(pA - 8));
    crcB = _mm_crc32_u64_t(crcB, *(uint64_t*)(pB - 8));
```

This is case 1: the last triplet. We do it normally for A and B, but NOT for C - that's our special case! Time to CLMUL:

```
    __m128i vK = _mm_cvtepu32_epi64(_mm_loadu_si128((__m128i*)(&lut[n - 1])));
```

This one's complex enough to tackle in pieces:

```
    _mm_loadu_si128((__m128i*)(&lut[n - 1]))
```

This loads an XMM register with 128 bits of data starting at the memory location of entry 'n - 1' from our LUT. Such a register can hold 4 of these 32-bit entries, so the register would look like:

```
    0        32        64        96        128
    ---------|---------|---------|---------
     [n-1]   | [n]     | [n+1]   | [n+2]
```

Now the outer expression is applied to that register:

```
    _mm_cvtepu32_epi64(...)
```

This expands the first two 32-bit values out to 64 bits, discarding the remaining 64 bits:

```
    0        32        64        96        128
    ---------|---------|---------|---------
     [n-1]             | [n]
```

Note that this would SEEM to suggest we read 128 bits out of the table, not 64, so if we access the last entry of the table (i.e. if n == 256), it would seem we are dangerously reading past the end of memory that we own.

If this is a concern, the usual solution would be to just add one more dummy 64-bit element at the end of the LUT.

However, it ISN'T actually a concern. I have written the combination of _mm_cvtepu32_epi64(_mm_loadu_si128()) deliberately: it is optimized on modern compilers (when compiling with optimizations on!) to the single instruction VPMOVZXDQ, which only reads 64 bits from memory since it knows the latter 64 bits of the 128 would be discarded anyway. Check your implementation to be sure, or just add the 64-bit dummy element to the end of the LUT.

Anyway: recall that these indices correspond to the precomputed table values for CRC(1 << 2x-64) and CRC(1 << x-64), respectively. They are now zero-extended out to 64-bit values, as required by the CLMUL instruction, ready to be CLMUL'd against crcA and crcB, respectively. So that's what we do:

```
    __m128i vA = _mm_clmulepi64_si128(_mm_cvtsi64_si128(crcA), vK, 0);
```

Move crcA into the first 64 bits of a vector register. CLMUL it by the first 64 bits of vK. Store the result in vA.

```
    __m128i vB = _mm_clmulepi64_si128(_mm_cvtsi64_si128(crcB), vK, 16);
```

Move crcB into the first 64 bits of a vector register. CLMUL it by the SECOND 64 bits of vK. Store the result in vB.

```
    crcA = _mm_crc32_u64(crcC, _mm_cvtsi128_si64(_mm_xor_si128(vA, vB)) ^ *(uint64_t*)(pC - 8));
```

This line is our "golden" result! It calculates:

```
    CRC(M) = CRC(CRC(D), (CRC(A) * CRC(1 << 2x-64)) ^ (CRC(B) * CRC(1 << x-64)) ^ E)
```

It's complex so we can break it apart too:

```
    _mm_xor_si128(vA, vB)
```

This line XORs the A and B vector registers together. At this point we have completed our CLMULs. The low 64 bits of vA contain:

```
    (CRC(A) * CRC(1 << 2x-64))
```

The low 64 bits of vB contain:

```
    (CRC(B) * CRC(1 << x-64))
```

So the line XORs those two quantities together. Next:

```
    _mm_cvtsi128_si64(...)
```

We move that result back out into a GPR.

```
    ... ^ *(uint64_t*)(pC - 8)
```

XOR that result with the final 8 bytes of C.

```
    crcA = _mm_crc32_u64(crcC, ...);
```

Perform the operation CRC(crcC, result on that result. Store that final result in crcA.

Next line:

```
    bytes -= 24 * n
```

We've just processed n blocks of 8 bytes each, in each of A, B, and C, for a total of 8*3*n = 24*n bytes of message processed. We update 'bytes' accordingly. The accumulated result is in crcA.

```
    pA = pC;
```

Recall that we set pC to the end of the C block, which is the end of the data that we just finished processing. That location therefore also represents the beginning of remaining unprocessed data, if any. We therefore advance pA to that point, ready for the next iteration of the while loop (if any).

```
    return crcA;
```

When we're done, we return crcA, as it has been accumulating our CRC as we've moved along the message.


This is almost it; we just have to tackle a few things I left off for clarity for a proper, production-ready implementation:

- Alignment. We are processing message data 64 bits at a time, and therefore loading 64 bits at a time in our CRC instructions. Most computer architectures like the incoming data to also be aligned to an 8 byte boundary when you do this, meaning the address where the data is located is divisible by 8. Modern x86 architectures will not crash if you do not obey this, but there may be a performance penalty, so it is potentially wise to advance a few bytes manually, just crunching CRC 1 byte at a time, until you're 8-byte-aligned, prior to beginning the main loop. The following expression calculates the amount of bytes needed to achieve this and then computes that many bytes of CRC, advancing bytes and decrementing pA as it goes:

```
    uint32_t toAlign = ((uint64_t)-(int64_t)pA) & 7;

    for (; toAlign && bytes; ++pA, --bytes, --toAlign)
        crcA = _mm_crc32_u8((uint32_t)crcA, *(uint8_t*)pA);
```

Note that this is often not something you have to think explicitly about if you're manipulating data which is inherently 8 bytes in nature. For example, if you create an array of uint64_t's in C/C++, it will inherently align itself to an 8-byte boundary if created via any sane method, so any consumer of the data can safely assume that it is aligned. However, in this case, we're taking a big buffer of data that isn't inherently sized in nature, so we have no expectation of alignment. Yet we CHOOSE to access it in 8-byte blocks for performance reasons. Therefore WE must be responsible for ensuring alignment if we want the best performance.


- Leaf size. The complexity and overhead of this approach to CRC only makes sense for data of a certain size, and so performance is significantly approved by checking for a minimum size of remaining message data (the "leaf size"), and if you're below that, instead of entering the main loop, it's faster to just complete the remainder using the naive hardware 64-bit CRC without parallelism. This also means you don't need switch cases below the leaf size, nor entries in the LUT, although this is a minor detail and can probably be left in for simplicity. The important part is to not enter the while loop if fewer bytes remain than the leaf size, and finish any remaining bytes afterward. The appropriate cutoff varies by architecture and should be a multiple of 24 since what we really want is a minimum n, and an increase of 1 in n increases the amount of message processed by 8*3=24 bytes. I use n=6 as my cutoff.

- Faster division by 24. Good compilers will optimize that bytes / 24 into a multiply and a shift, but we know something the compiler doesn't: in that 'bytes / 24', bytes will never be larger than 256*24. Actually, compilers SHOULD know that, since it's a simple ternary that obviously excludes any larger value, but they don't currently take advantage of this. But we can do so manually to produce a potentially faster division and smaller code size:

```
    x * 2731 >> 16
```

This expression is equivalent to x / 24 for x <= 256*24. See the outstanding book Hacker's Delight by Henry S. Warren, Jr., chapter 10, for a discussion of equivalences to integer division by constants.


- More compact representation of that giant switch statement. We can use a series of "doubler" preprocessor macros to repeatedly duplicate a single case implementation until we have all of them:

```
    #define CRC_ITER(i) case i:                             \
    crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA - 8*(i)));   \
    crcB = _mm_crc32_u64(crcB, *(uint64_t*)(pB - 8*(i)));   \
    crcC = _mm_crc32_u64(crcC, *(uint64_t*)(pC - 8*(i)));

    #define X0(n) CRC_ITER(n);
    #define X1(n) X0(n+1) X0(n)
    #define X2(n) X1(n+2) X1(n)
    #define X3(n) X2(n+4) X2(n)
    #define X4(n) X3(n+8) X3(n)
    #define X5(n) X4(n+16) X4(n)
    #define X6(n) X5(n+32) X5(n)
    #define X7(n) X6(n+64) X6(n)
    #define CRC_ITERS_256_TO_2() do {X0(256) X1(254) X2(250) X3(242) X4(226) X5(194) X6(130) X7(2)} while(0)

    ...
        switch (n)
            CRC_ITERS_256_TO_2();
    ...
```


- Starting with an existing CRC value. A production-ready CRC implementation should allow the user to supply an existing value for the R register, just like the x86 crc32 instruction does. This is easy for us to incorporate: instead of initializing crcA to 0 at the beginning, support an additional function argument, which defaults to 0, that the user may use if they wish in order to pass in a nonzero initial CRC value, and initialize crcA to that:

```
    uint32_t crc32(const uint8_t* M, uint32_t bytes, uint32_t prev = 0) {
        ...
        uint64_t crcA = (uint64_t)prev;
```

- Inverting or otherwise modifying the initial and final CRC values. Production-ready CRC implementations often initialize the CRC register to, instead of just 'prev', prev XOR'd with some constant, often 0xFFFFFFFF, which is equivalent to a bitwise inversion, i.e. a bitwise NOT:

```
    uint64_t crcA = (uint64_t)(uint32_t)(~prev);
```

And similarly for the output at the end:

```
    return ~(uint32_t)crcA;
```

Note that this does NOT make a CRC more secure or more reliable or anything. However, it does acknowledge and guard against a particular fact: all messages are not created equal. The sorts of messages that we deal with in real life are often unusually likely to begin and end with long strings of 0 bits; at least much more so than any other particular combination of bits. Trailing zeros do change the CRC, but leading zeros do not, if the R register starts at 0. This means our CRC would be unable to distinguish between the real message M and another message 0M with any number of added leading zeros! So we initialize the R register to something other than 0 to guard against this.

Again, this isn't actually making the CRC more secure in the general case: we have instead introduced some new case of a leading bit pattern that does not mutate the state of the CRC. But whatever that pattern is, it's WAY less likely to be the start of the sorts of messages we typically send in the real world, compared to all 0s, so it makes more sense to guard the 0 case.


- Taking a void* instead of a uint8_t* for the message input. We're already casting the message to a uint64_t right away, so we should allow a convenient interface for clients. Taking a void* allows clients to pass a buffer of any kind without having to cast on their end, so it's good practice:

```
    uint32_t crc32(const void* M...
```

- A tweaked implementation for the AMD Jaguar x86 CPU found in the PS4 and the Xbox One consoles. These CPUs support everything we used in our golden implementation, so it could run just fine, and be quite fast. However, the performance characteristics of their crc32 instruction are slightly different. Recall that the crc32 instruction of modern high-end Intel CPUs has a latency of 3 cycles and a reciprocal throughput of 1 cycle, which is why we needed to break our M into 3 equal chunks to maximize throughput.

In contrast, the crc32 instruction on the AMD Jaguar CPU, which is a highly efficient design but is nonetheless vastly simpler and cheaper than a modern desktop CPU, has a latency of 3 cycles and a reciprocal throughput of 2 cycles, not 1. Therefore it is optimal to break our M into 2 equal chunks, not 3, as there will never be more than 2 CRCs in flight in parallel. I won't go into detail here, but the same approach is used to create a slightly tweaked version of the golden implementation that is tuned for Jaguar. A production-ready such implementation for Jaguar is provided in the code that accompanies this document.



Putting it all together:


### OPTION 13: Golden ###

```
    uint32_t crc32(const uint8_t* M, uint32_t bytes, uint32_t prev = 0) {
        uint64_t pA = (uint64_t)M;
        uint64_t crcA = (uint64_t)(uint32_t)(~prev);
        uint32_t toAlign = ((uint64_t)-(int64_t)pA) & 7;

        for (; toAlign && bytes; ++pA, --bytes, --toAlign)
            crcA = _mm_crc32_u8((uint32_t)crcA, *(uint8_t*)pA);

        while (bytes >= LEAF_SIZE) {
            const uint32_t n = bytes < 256 * 24 ? bytes * 2731 >> 16 : 256;
            pA += 8 * n;
            uint64_t pB = pA + 8 * n;
            uint64_t pC = pB + 8 * n;
            uint64_t crcB = 0, crcC = 0;
            switch (n)
                CRC_ITERS_256_TO_2();

            crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA - 8));
            crcB = _mm_crc32_u64(crcB, *(uint64_t*)(pB - 8));
            const __m128i vK = _mm_cvtepu32_epi64(_mm_loadu_si128((__m128i*)(&lut[n - 1])));
            const __m128i vA = _mm_clmulepi64_si128(_mm_cvtsi64_si128(crcA), vK, 0);
            const __m128i vB = _mm_clmulepi64_si128(_mm_cvtsi64_si128(crcB), vK, 16);
            crcA = _mm_crc32_u64(crcC, _mm_cvtsi128_si64(_mm_xor_si128(vA, vB)) ^ *(uint64_t*)(pC - 8));

            bytes -= 24 * n;
            pA = pC;
        }

        for (; bytes >= 8; bytes -= 8, pA += 8)
            crcA = _mm_crc32_u64(crcA, *(uint64_t*)(pA));

        for (; bytes; --bytes, ++pA)
            crcA = _mm_crc32_u8((uint32_t)crcA, *(uint8_t*)(pA));

        return ~(uint32_t)crcA;
    }
```

And there we have it! A production-ready CRC32 implementation supporting messages of any length, with any alignment, with world-class performance on modern Intel x86 architectures. The LUT can be stored in the program or generated at runtime from the code shown earlier.


So how'd we do? As with Option 12, we should be able to predict our performance: we expect a peak theoretical completion of 1 crc32 per cycle, each processing 8 bytes (64 bits). Therefore, we have a theoretical ceiling of 64 bits per cycle.

Do we achieve it?

Short answer: YES. For a large workload, we achieve an empirical throughput of approximately 62 bits per cycle.


## Performance comparison table: ##

  Approach                                 |  Performance
-------------------------------------------|------------------------
  OPTION 1: Check Carry Flag and Jump      |   0.09 bits per cycle
  OPTION 2: Multiply Mask                  |   0.20 bits per cycle
  OPTION 3: Bit Mask                       |   0.25 bits per cycle
  OPTION 4: Conditional Move               |   0.33 bits per cycle
  OPTION 5: Compiler Output                |   0.33 bits per cycle
  OPTION 6: 1-byte Tabular                 |   1.10 bits per cycle
  OPTION 7: 2-byte Tabular                 |   1.60 bits per cycle
  OPTION 8: 4-byte Tabular                 |   2.70 bits per cycle
  OPTION 9: 8-byte Tabular                 |   4.80 bits per cycle
  OPTION 10: 16-byte Tabular               |   8.00 bits per cycle
  OPTION 11: 1-byte Hardware-accelerated   |   2.66 bits per cycle
  OPTION 12: 8-byte Hardware-accelerated   |  21.30 bits per cycle
  OPTION 13: Golden                        |  62.00 bits per cycle


### Some final thoughts: ###

#### Crazy optimizations: ####

This implementation doesn't compromise on performance in any significant way, but there were times when we made some sacrifices in order to keep it (relatively) simple and implementable in a high-level language. Here's a quick list of next-level tweaks that could improve performance or code size further if you have a VERY specific need, roughly organized in decreasing order of significance:

- Specialize! If you know anything about your data, take advantage of that fact so you don't need a fully general implementation. Can you guarantee your message will always be aligned? Then you don't need the alignment logic. Always a multiple of 8 bytes in length? Then you don't need the last 1-byte-at-a-time cleanup loop. Always a multiple of the leaf size in length? Then you don't need the second-to-last 8-byte-at-a-time cleanup loop. Always an EXACT fixed size? Then you don't need the LUT! If it's a long message, you still need the loop, but just store the few constants you need. If it's short, you don't even need the loop, and can process the whole message in one go.

- If you rewrite in assembly, you can do the direct-jump trick described earlier, manually writing out the instruction bytes of the CRCs to enforce that they're all the same size. Then you don't need a jump table.

- You don't need switch case blocks for indices smaller than the leaf size.

- You don't need LUT entries smaller than the leaf size.

- If you rewrite in assembly, you can pick up a few small savings here and there which are very specific, such as better choices of register coloring, fewer spills to memory, and much better use of flags for directly branching after shifts and other operations.


#### Compilers are not very good: ####

A theme throughout this write-up has been how ALL modern compilers routinely fail to fully optimize (either for size or speed) ALL KINDS of different problems, which required us to repeatedly compromise or work around them. The following come to mind as a summary:

- NO compilers can match Option 4 for a direct implementation of the CRC algorithm as they do not properly use flags for conditional moves and branches after shifts
- MSVC gets the 32-bit-ness of _mm_crc32_u64's first argument and return value wrong
- MSVC doesn't unroll the direct implementation of clmul, and doesn't provide an option to force it
- MSVC doesn't generate conditional moves when it should in the direct implementation of clmul
- NO compilers properly unroll the loop of the Golden method unless you manually write it out fully unrolled
- NO compilers generate a direct-jump for the Golden method, instead generating a jump table
- NO compilers pack the jump table entries into 2 bytes (or 1), instead consuming 4 bytes per entry
- GCC fails to generate an efficient jump table, instead generating trampolines
- NO compilers detect that a simpler form of the 'bytes / 24' expression is possible due to the known upper bound on 'bytes'
- NO compilers properly use flags after shifts to reduce comparisons and code size

And, perhaps most importantly of all, all of the above is applied to an already VERY specific and painstakingly optimized approach to CRC. Any attempt to naively express a CRC fails so spectacularly that its performance is literally more than 150x slower than our final Golden result. And all of that arises from complex reasoning about algorithmic changes and mathematical transformations in the CRC space. None of that is space a compiler could not ever HOPE to reason about, no matter how advanced.

And then, even in the tiny space that a compiler COULD reason about, all three major compilers routinely failed to do the right thing, requiring workarounds or accepting suboptimal performance or code size in order to stay in a high-level language. Requiring machine-instruction intrinsics. Requiring contorting the preprocessor to manually unroll a loop.

Anyone who is serious about programming should not claim nor expect compilers to replace comprehensive expertise and utmost attention to performance.

Be FAMILIAR with your hardware. The hardware IS the platform. Know it. Know the assembly you want. Make the compiler listen.


Happy CRC'ing!

Please feel free to contact me with any questions or corrections.

-komrad36
