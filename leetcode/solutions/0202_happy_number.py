class Solution:
    def isHappy(self, n: int) -> bool:
        
        seen = set()

        while n != 1:
            if n in seen:
                return False

            seen.add(n)

            n = self.get_next(n)

        return True

    def get_next(self, n):

        total = 0

        while n > 0:
            digit = n % 10
            total += digit** 2

            n = n // 10

        return total