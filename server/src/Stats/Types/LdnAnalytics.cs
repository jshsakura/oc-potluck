using LanPlayServer.Utils;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace LanPlayServer.Stats.Types
{
    public class LdnAnalytics: INotifyPropertyChanged
    {
        private static readonly LdnAnalyticsSerializerContext SerializerContext = new(JsonHelper.GetDefaultSerializerOptions(false));

        public event PropertyChangedEventHandler PropertyChanged;

        private int _totalGameCount;
        private int _privateGameCount;
        private int _publicGameCount;
        private int _inProgressCount;
        private int _masterProxyCount;
        private int _totalPlayerCount;
        private int _privatePlayerCount;
        private int _publicPlayerCount;

        public int TotalGameCount
        {
            get => _totalGameCount;
            set
            {
                if (_totalGameCount != value)
                {
                    _totalGameCount = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int PrivateGameCount
        {
            get => _privateGameCount;
            set
            {
                if (_privateGameCount != value)
                {
                    _privateGameCount = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int PublicGameCount
        {
            get => _publicGameCount;
            set
            {
                if (_publicGameCount != value)
                {
                    _publicGameCount = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int InProgressCount
        {
            get => _inProgressCount;
            set
            {
                if (_inProgressCount != value)
                {
                    _inProgressCount = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int MasterProxyCount
        {
            get => _masterProxyCount;
            set
            {
                if (_masterProxyCount != value)
                {
                    _masterProxyCount = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int TotalPlayerCount
        {
            get => _totalPlayerCount;
            set
            {
                if (_totalPlayerCount != value)
                {
                    _totalPlayerCount = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int PrivatePlayerCount
        {
            get => _privatePlayerCount;
            set
            {
                if (_privatePlayerCount != value)
                {
                    _privatePlayerCount = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int PublicPlayerCount
        {
            get => _publicPlayerCount;
            set
            {
                if (_publicPlayerCount != value)
                {
                    _publicPlayerCount = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public string ToJson()
        {
            return JsonHelper.Serialize(this, SerializerContext.LdnAnalytics);
        }

        private void NotifyPropertyChanged([CallerMemberName] string propertyName = "")
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}