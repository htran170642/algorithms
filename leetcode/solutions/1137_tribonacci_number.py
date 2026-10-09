class Solution:
    def tribonacci(self, n: int) -> int:
        if n <= 1:
            return n
        if n == 2:
            return 1

        t0 = 0
        t1 = 1
        t2 = 1
       
        tn = 0
        for i in range(3, n+1):
            tn = t0 + t1 + t2
            t0 = t1
            t1 = t2
            t2 = tn

        return tn