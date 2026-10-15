class Solution:
    def myAtoi(self, s: str) -> int:
        MAX_VALUE = 2**31 - 1
        MIN_VALUE = -(2**31)

        sign = 1

        i = 0
        while i < len(s):
            if s[i] == " ":
                i += 1
            else:
                break
        print(i, s[i:])

        if i < len(s) and s[i] in "-+":
            if s[i] == '-':
                sign = -1

            i += 1

        num =0

        print("xxx")
        while i < len(s) and s[i].isdigit():
            digit = int(s[i])
            print(digit)

            if num > MAX_VALUE//10 or (num == MAX_VALUE//10 and digit > 7):
                return MAX_VALUE if sign == 1 else MIN_VALUE

            num = num*10 + digit
            i += 1
            print(num)

        return num*sign