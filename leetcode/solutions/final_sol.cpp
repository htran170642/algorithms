#include<bits/stdc++.h>
using namespace std;
typedef long long ll;
const int MOD = 1e9 + 7;
const int N = 5e5 + 7;
ll n, x;
pair<int, int> p[507];
ll egg[N], pre[N];
namespace subtask1
{
	void solve()
	{
		cout << 1ll * (x - 1) * p[1].second + p[1].first;
	}
}
namespace subtask2
{
	void solve()
	{
		for(int i = 1; i <= n; i++)
		{
			for(int j = p[i].first; j < N; j += p[i].second)
				egg[j]++;
		}
		ll ans = 0;
		int i = 1;
		while(x > 0)
		{
			if(egg[i] > 0) ans = i, x -= egg[i];
			i++;
		}
		cout << ans;
	}
}
namespace subtask3
{
	const ll CAP = (ll)2e15; // chan gia tri de tranh tran so (x <= 1e15 nen chan o 2e15 la an toan)

	// dem tong so trung cua tat ca cac con ga tinh den giay T
	ll countEggs(ll T)
	{
		ll total = 0;
		for(int i = 1; i <= n; i++)
		{
			if(T >= p[i].first)
			{
				ll c = (T - p[i].first) / p[i].second + 1;
				if(c > CAP) c = CAP;
				total += c;
				if(total > CAP) total = CAP;
			}
		}
		return total;
	}

	void solve()
	{
		// nhi phan tim T nho nhat sao cho countEggs(T) >= x
		ll lo = 1, hi = (ll)2e18;
		while(lo < hi)
		{
			ll mid = lo + (hi - lo) / 2;
			if(countEggs(mid) >= x) hi = mid;
			else lo = mid + 1;
		}
		cout << lo;
	}
}
int main()
{
	freopen("test.inp","r",stdin);
	freopen("test.out","w",stdout);
	ios_base::sync_with_stdio(0);
	cin.tie(0); cout.tie(0);
	cin >> n >> x;
	for(int i = 1; i <= n; i++)
		cin >> p[i].first >> p[i].second;
	// if(n == 1)
	// {
	// 	subtask1::solve();
	// 	return 0;
	// }
	// if(n <= 20 && x <= 1000)
	// {
	// 	subtask2::solve();
	// 	return 0;
	// }
	// if(n <= 20)
	// {
	// 	subtask3::solve();
	// 	return 0;
	// }

    subtask3::solve();
    return 0;
}
